[-> English](README.md)

# Soma Kernel — NASM + C + Jeti 三语 i386 内核

一个 32 位保护模式微内核，同时混编三种语言：

- **NASM**  — 引导扇区 (`boot/boot.asm`)、内核入口与 IDT 桩 (`kernel/entry.asm`、`kernel/isr.asm`)
- **C**     — VGA 文本屏、串口、`kprintf`、freestanding `mem*/str*`、IDT/PIC/PIT (`kernel/kernel.c`、`kernel/hw.c`)
- **Jeti**  — 内核模块 (`jeti/soma_core.jeti`)：`jetic -rewrite-jeti` 转译成 C 后一起编译

关键点：**转译出的 C 不依赖 libc**（无 `printf`/`malloc`，唯一头是 `#include <string.h>` 且不调用任何 libc 函数）。
Jeti 通过 extern 声明直接调用内核的 `kputs/kputdec/kputhex` 输出，内核反过来直接调用 Jeti 的
`soma_fib/soma_gcd/soma_rotl/soma_fnv1a/soma_xorshift`，Jeti 的 `soma_io_wait` 用内联 asm
（`outb` 到端口 0x80）在裸机生效。

## Jeti 高级特性（不依赖 Foundation）

`jeti/soma_core.jeti` 用上了编译器的类系统，且**在裸机内核里真正跑通**：

- **`@namespace SomaCore`** — 类名自动加 `SomaCore__` 前缀（`SomaCore::Calculator`）
- **`@interface Calculator`（隐式根类）** — 不写 `: NPObject`，直接定义类；转译产物里类只有
  `isa` + `retain_count` + ivar，无任何 Foundation 依赖
- **类方法派发** — `[SomaCore::Calculator compute:21]` 转译为直接函数调用
  `SomaCore__Calculator_compute_(&jeti_..._class, sel, 21)`
- **实例方法派发** — `[acc add:7]` 转译为 `recv->isa->vtable->methods[INDEX]`，裸机可用

为支持类系统，内核提供最小 freestanding runtime 头（`include/jeti/runtime.h`），只定义转译代码
需要的类型（`SEL`/`NPClass`/`NPObject`/`id`）和符号（`jeti___jeti_root_class`），
Makefile 用 `-include jeti/runtime.h` 注入每个编译单元；`jeti_meta_init()`（转译器弱符号生成）
在 `kmain` 里先调用，然后 C 侧手工构造实例（`isa = &jeti_..._class`）交给 Jeti 实例方法使用。

## 构建与运行

```bash
make all            # 产物: build/{kernel.elf,kernel.bin,boot.bin,floppy.img}
./run.sh            # 无头测试: qemu 运行，校验 isa-debug-exit 退出码 33，打印 serial.log
./run.sh --gui      # 弹原生窗口(cocoa)显示 VGA 文本屏，内核打印完 halt 后窗口保持打开
```

`run.sh --gui` 输出示例（VGA 屏与终端串口同步）：

```
=== SOMA KERNEL (i686, 32-bit protected mode) ===
built: clang + nasm + jetic transpile, ran under qemu-system-i386
[jeti] soma_core_boot()
       fib(10)=55
       gcd(1071,462)=21
       fnv1a("soma-kernel")=0x51b97b15
       rotl(12345678,8)=0x34567812
       xorshift: 87985aa5 155b24a3 4820f4c4 81b3ac98 703a0788
[jeti] class method [SomaCore::Calculator compute:21] = 43
[jeti] instance methods on C-created obj: add:7 -> 7, add:35 -> 42, value = 42
[c] call Jeti: fib(15)=610 gcd(1071,462)=21
[c] call Jeti: rotl(0x12345678,4)=0x23456781
[c] call Jeti: fnv1a("jeti")=0x944c9dcf
[c] interrupts on; waiting for PIT ticks...
[c] timer reached tick=50 (IRQ0+PIT ok)

SOMA KERNEL OK
```

## 工具链

| 组件 | 工具 | 说明 |
|------|------|------|
| Jeti→C | `../../target/debug/jetic -rewrite-jeti` | 输出仅含 `#include <string.h>` |
| C 编译 | `clang -target i386-none-elf -m32 -ffreestanding -fno-builtin -nostdlib -nostdinc -Iinclude` | freestanding，自带头 |
| 汇编 | `nasm -f elf32` / `-f bin` | 内核对象 / 引导扇区 |
| 链接 | `i686-elf-ld -m elf_i386 -T linker.ld` | Apple `ld` 无 `elf_i386`，须用 GNU ld |
| 运行 | `qemu-system-i386` + `isa-debug-exit` | 无头测试退出码 33 = PASS |

## 目录结构

```
examples/04_soma-kernel/
├── boot/boot.asm      引导扇区: 实模式→读盘到 0x10000→A20→GDT→保护模式→跳内核
├── kernel/
│   ├── entry.asm      32 位入口: 设栈、清 BSS、call kmain
│   ├── isr.asm        isr0..isr47 桩 + isr_stubs 表 + 统一 C 回调
│   ├── kernel.c       VGA/串口/kprintf/mem*/str*/kmain（含 Jeti 互调）
│   └── hw.c           IDT/PIC 重映射/PIT/ISR handler/tick
├── jeti/soma_core.jeti  Jeti 内核模块（@namespace + @interface 隐式根类，无 Foundation）
├── include/           freestanding 头: stdint/stddef/stdarg/stdbool/string + jeti/runtime.h 最小版
├── linker.ld          链接脚本 (入口 kernel_entry, 起点 0x10000)
├── Makefile / run.sh
└── README.md
```

## 注意

- Jeti 模块不能用闭包；整数字面量不能带 `u` 后缀（解析器限制）。
- 内联 asm 模板：只要带操作数 section，字面 `%` 必须写成 `%%`；`outb %b0, $0x80` 用操作数引用。
- 类系统裸机要点：`jeti/runtime.h` 的 include guard 必须与 `crates/codegen` 生成的
  `__JETI_ROOT_DEFINED`/`NPOBJECT_DEFINED` 一致（否则重复定义）；`jeti___jeti_root_class`
  由 kernel.c 提供，`jeti_meta_init()` 先于类使用调用。
- `tests/**/*.jeti` 会被 `test_all.py` 默认套件扫到，`soma-kernel` 已在套件中排除，用 `./run.sh` 独立验证。
