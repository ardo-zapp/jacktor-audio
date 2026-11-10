# amp-status - Audio & Amplifier Diagnostics
PROJECT_ROOT ?= $(abspath $(CURDIR)/../../../..)
TOOLCHAIN ?= $(PROJECT_ROOT)/tools/aarch64-linux-musl-cross
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/aarch64-linux-musl-
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
CFLAGS = -O2 -Wall -Wextra -static -pie -D_GNU_SOURCE
TARGET = amp-status
SRC = amp-status.c
MODULE_BIN = ../../modul/jacktor-audio-framework/system/bin

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	$(STRIP) --strip-all $(TARGET)
	cp $(TARGET) $(MODULE_BIN)/$(TARGET)
	chmod 755 $(MODULE_BIN)/$(TARGET)
	rm -f $(TARGET) *.o

clean:
	rm -f $(TARGET)

.PHONY: all install clean