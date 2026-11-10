# jacktor-api - Native High-Speed System, Kernel & Telemetry Bridge API Service
PROJECT_ROOT ?= $(abspath $(CURDIR)/../../../..)
TOOLCHAIN ?= $(PROJECT_ROOT)/tools/aarch64-linux-musl-cross
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/aarch64-linux-musl-
CC = $(CROSS_COMPILE)gcc
STRIP = $(CROSS_COMPILE)strip
CFLAGS = -O2 -Wall -Wextra -static -pie -D_GNU_SOURCE
TARGET = jacktor-api
SRC = jacktor-api.c
MODULE_BIN = ../../modul/jacktor-audio-framework/system/bin
CGI_DIR = ../../modul/jacktor-audio-framework/webroot/cgi-bin

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) $(CFLAGS) -o $(TARGET) $(SRC)

install: $(TARGET)
	$(STRIP) --strip-all $(TARGET)
	mkdir -p $(MODULE_BIN) $(CGI_DIR)
	cp -f $(TARGET) $(MODULE_BIN)/$(TARGET)
	chmod 755 $(MODULE_BIN)/$(TARGET)
	rm -f $(CGI_DIR)/api.sh $(CGI_DIR)/api
	ln -sf ../../system/bin/$(TARGET) $(CGI_DIR)/$(TARGET)
	rm -f $(TARGET) *.o

clean:
	rm -f $(TARGET) *.o

.PHONY: all install clean
