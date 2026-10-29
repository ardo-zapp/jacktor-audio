ifndef KP_KERNEL_DIR
    KP_KERNEL_DIR = /tmp/KernelPatch/kernel
endif

CC = /tmp/aarch64-linux-musl-cross/bin/aarch64-linux-musl-gcc
LD = /tmp/aarch64-linux-musl-cross/bin/aarch64-linux-musl-ld

INCLUDE_DIRS := . include $(KP_KERNEL_DIR)/include $(KP_KERNEL_DIR)/patch/include $(KP_KERNEL_DIR)/linux/include $(KP_KERNEL_DIR)/linux/arch/arm64/include $(KP_KERNEL_DIR)/linux/tools/arch/arm64/include
INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(dir))

CFLAGS = $(INCLUDE_FLAGS) -Wall -O2 -fno-PIC -mno-outline-atomics -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-exceptions -fno-stack-protector -fno-common -ffixed-x18

objs := usb_serial.o

all: usb-serial.kpm

usb-serial.kpm: ${objs}
	${CC} -r -o $@ $^
	/tmp/aarch64-linux-musl-cross/bin/aarch64-linux-musl-strip -g --strip-unneeded -R .eh_frame -R .rela.eh_frame -R .comment -R .note.GNU-stack $@

%.o: %.c
	${CC} $(CFLAGS) -c -o $@ $<

.PHONY: clean
clean:
	rm -rf *.kpm *.o
