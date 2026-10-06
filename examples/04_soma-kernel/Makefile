# examples/04_soma-kernel/Makefile
# Build a 32-bit protected-mode kernel from NASM + C + Nopa, bootable floppy image.

NOPAC   := ../../target/debug/nopac
CC      := clang
NASM    := nasm
LD      := i686-elf-ld
OBJCOPY := i686-elf-objcopy

CFLAGS   := -target i386-none-elf -m32 -ffreestanding -fno-builtin \
            -fno-stack-protector -fno-pic -fno-pie -mno-sse -mno-mmx \
            -nostdlib -nostdinc -Iinclude -I../../include -D__NOPA_FREESTANDING \
            -Wall -Wextra -Wno-unused-parameter -O2
NASMFLAGS := -f elf32
LDFLAGS  := -m elf_i386 -T linker.ld -nostdlib

BUILD := build
OBJS    := $(BUILD)/entry.o $(BUILD)/isr.o $(BUILD)/hw.o $(BUILD)/kernel.o $(BUILD)/soma_core.o $(BUILD)/runtime_freestanding.o

all: $(BUILD)/floppy.img

# ---------- kernel objects ----------
$(BUILD)/%.o: kernel/%.asm
	@mkdir -p $(BUILD)
	$(NASM) $(NASMFLAGS) -o $@ $<

$(BUILD)/%.o: kernel/%.c kernel/kernel.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------- Nopa -> C transpile ----------
$(BUILD)/soma_core.c: nopa/soma_core.np
	@mkdir -p $(BUILD)
	$(NOPAC) -rewrite-nopa -ffreestanding -o $@ $<

$(BUILD)/soma_core.o: $(BUILD)/soma_core.c
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------- bare-metal runtime (bump allocator: nopa_alloc/nopa_release) ----------
$(BUILD)/runtime_freestanding.o: ../../include/nopa/runtime_freestanding.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

# ---------- link + raw binary ----------
$(BUILD)/kernel.elf: $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

$(BUILD)/kernel.bin: $(BUILD)/kernel.elf
	$(OBJCOPY) -O binary $< $@
	@echo "kernel.bin: $$(wc -c < $@) bytes"

# ---------- boot sector (sector count derived from kernel.bin) ----------
$(BUILD)/boot.bin: boot/boot.asm $(BUILD)/kernel.bin
	@mkdir -p $(BUILD)
	@sz=$$(wc -c < $(BUILD)/kernel.bin); \
	 secs=$$(( (sz + 511) / 512 )); \
	 [ $$secs -lt 1 ] && secs=1; \
	 echo "KERNEL_SECTORS=$$secs"; \
	 $(NASM) -f bin -DKERNEL_SECTORS=$$secs -o $@ $<

# ---------- bootable 1.44MB floppy image ----------
$(BUILD)/floppy.img: $(BUILD)/boot.bin $(BUILD)/kernel.bin
	@rm -f $@
	cat $(BUILD)/boot.bin > $@
	dd if=$(BUILD)/kernel.bin of=$@ bs=512 seek=1 conv=notrunc status=none
	truncate -s 1474560 $@
	@ls -l $@
run: all
	./run.sh

run-gui: all
	./run.sh --gui

qemu: all
	qemu-system-i386 -drive file=$(BUILD)/floppy.img,format=raw,if=floppy -display none -serial stdio -no-reboot

clean:
	rm -rf $(BUILD)

.PHONY: all run run-gui qemu clean
