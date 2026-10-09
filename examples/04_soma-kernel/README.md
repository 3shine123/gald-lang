[-> 中文](CHINESE.md)

# Soma Kernel — a 32-bit i386 kernel in NASM + C + Ovic

A 32-bit protected-mode microkernel that mixes three languages:

- **NASM** — boot sector (`boot/boot.asm`), kernel entry + IDT stubs (`kernel/entry.asm`, `kernel/isr.asm`)
- **C** — VGA text console, serial, `kprintf`, freestanding `mem*/str*`, IDT/PIC/PIT (`kernel/kernel.c`, `kernel/hw.c`)
- **Ovic** — kernel module (`ovic/soma_core.ov`), transpiled to C via `ovicc -rewrite-ovic`

Key point: **the transpiled C has no libc dependency** (no `printf`/`malloc`; the only header is `#include <string.h>` and it calls zero libc functions). Ovic calls the kernel's `kputs/kputdec/kputhex` through extern declarations, the kernel calls Ovic's `soma_fib/soma_gcd/soma_rotl/soma_fnv1a/soma_xorshift` directly, and Ovic's `soma_io_wait` uses inline asm (`outb` to port 0x80) that works on bare metal.

## Ovic advanced features (no Foundation)

`ovic/soma_core.ov` uses the compiler's class system, running on bare metal:

- **`@namespace SomaCore`** — class names get a `SomaCore__` prefix (`SomaCore::Calculator`)
- **`@interface Calculator` (implicit root class)** — no `: NPObject`; the transpiled struct is just `isa` + `retain_count` + ivars, no Foundation
- **Class method dispatch** — `[SomaCore::Calculator compute:21]` becomes a direct call `SomaCore__Calculator_compute_(&ovic_..._class, sel, 21)`
- **Instance method dispatch** — `[acc add:7]` becomes `recv->isa->vtable->methods[INDEX]`, verified on bare metal

To support the class system the kernel ships a minimal freestanding runtime header (`include/ovic/runtime.h`) with only the types the transpiled code needs (`SEL`/`NPClass`/`NPObject`/`id`) and the `ovic___ovic_root_class` symbol; the Makefile injects it into every TU with `-include ovic/runtime.h`. `ovic_meta_init()` (a weak symbol emitted by the transpiler) is called from `kmain` first, then C hand-builds an instance (`isa = &ovic_..._class`) for Ovic's instance methods.

## Build & run

```bash
make all            # outputs: build/{kernel.elf,kernel.bin,boot.bin,floppy.img}
./run.sh            # headless test: boot in qemu, verify isa-debug-exit code 33, print serial.log
./run.sh --gui      # native window (cocoa) showing the VGA text screen; kernel halts, window stays open
```

`./run.sh --gui` output (VGA screen and terminal serial are in sync):

```
=== SOMA KERNEL (i686, 32-bit protected mode) ===
built: clang + nasm + ovicc transpile, ran under qemu-system-i386
[ovic] soma_core_boot()
       fib(10)=55
       gcd(1071,462)=21
       fnv1a("soma-kernel")=0x51b97b15
       rotl(12345678,8)=0x34567812
       xorshift: 87985aa5 155b24a3 4820f4c4 81b3ac98 703a0788
[ovic] class method [SomaCore::Calculator compute:21] = 43
[ovic] instance methods on C-created obj: add:7 -> 7, add:35 -> 42, value = 42
[c] call Ovic: fib(15)=610 gcd(1071,462)=21
[c] call Ovic: rotl(0x12345678,4)=0x23456781
[c] call Ovic: fnv1a("ovic")=0x944c9dcf
[c] interrupts on; waiting for PIT ticks...
[c] timer reached tick=50 (IRQ0+PIT ok)

SOMA KERNEL OK
```

## Toolchain

| Component | Tool | Notes |
|-----------|------|-------|
| Ovic→C | `../../target/debug/ovicc -rewrite-ovic` | output only `#include <string.h>` |
| C compile | `clang -target i386-none-elf -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Iinclude` | freestanding, self-hosted headers |
| Assembly | `nasm -f elf32` / `-f bin` | kernel objects / boot sector |
| Link | `i686-elf-ld -m elf_i386 -T linker.ld` | Apple `ld` has no `elf_i386`, GNU ld required |
| Run | `qemu-system-i386` + `isa-debug-exit` | headless exit code 33 = PASS |

## Layout

```
examples/04_soma-kernel/
├── boot/boot.asm      boot sector: real mode → read disk to 0x10000 → A20 → GDT → PM → kernel
├── kernel/
│   ├── entry.asm      32-bit entry: stack, BSS zeroing, call kmain
│   ├── isr.asm        isr0..isr47 stubs + isr_stubs table + unified C callback
│   ├── kernel.c       VGA/serial/kprintf/mem*/str*/kmain (incl. Ovic interop)
│   └── hw.c           IDT/PIC remap/PIT/ISR handler/tick
├── ovic/soma_core.ov  Ovic kernel module (@namespace + @interface implicit root, no Foundation)
├── include/           freestanding headers: stdint/stddef/stdarg/stdbool/string + minimal ovic/runtime.h
├── linker.ld          link script (entry kernel_entry, base 0x10000)
├── Makefile / run.sh
└── README.md
```

## Notes

- No closures in the Ovic module; integer literals must not use the `u` suffix (parser limitation).
- Inline asm templates: with an operand section, a literal `%` must be `%%`; `outb %b0, $0x80` uses operand references.
- Bare-metal class support: the `ovic/runtime.h` include guards must match codegen's `__OVIC_ROOT_DEFINED`/`NPOBJECT_DEFINED`; `ovic___ovic_root_class` is provided by kernel.c and `ovic_meta_init()` runs before any class use.
- `test_all.py` scans `tests/**/*.ov`; `soma-kernel` is excluded from the default suite — verify with `./run.sh` instead.
