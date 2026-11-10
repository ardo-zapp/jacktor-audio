# socat - Multipurpose Relay (Linux/Android ARM64 Musl Static Build)
# Ported and adapted for Jacktor Audio Framework by Ardo Zapp
# Upstream Author: Gerhard Rieger (http://www.dest-unreach.org/socat/)
# Licensed under GNU General Public License v2.0 (GPL-2.0-only)
PROJECT_ROOT ?= $(abspath $(CURDIR)/../../../..)
TOOLCHAIN ?= $(PROJECT_ROOT)/tools/aarch64-linux-musl-cross
CROSS_COMPILE ?= $(TOOLCHAIN)/bin/aarch64-linux-musl-
CC = $(CROSS_COMPILE)gcc
AR = $(CROSS_COMPILE)ar
RANLIB = $(CROSS_COMPILE)ranlib
STRIP = $(CROSS_COMPILE)strip

CFLAGS = -O2 -static -D_GNU_SOURCE -DHAVE_CONFIG_H -I. -Wall -Wno-parentheses
LDFLAGS = -static
LIBS = -lutil -lrt

TARGET = socat
MODULE_BIN = ../../modul/jacktor-audio-framework/system/bin

XIOSRCS = xioinitialize.c xiohelp.c xioparam.c xiodiag.c xioopen.c xioopts.c \
	xiosignal.c xiosigchld.c xioread.c xiowrite.c \
	xiolayer.c xioshutdown.c xioclose.c xioexit.c \
	xio-process.c xio-fd.c xio-fdnum.c xio-stdio.c xio-pipe.c \
	xio-socketpair.c xio-gopen.c xio-creat.c xio-file.c xio-named.c \
	xio-socket.c xio-interface.c xio-listen.c xio-unix.c xio-vsock.c \
	xio-ip.c xio-ip4.c xio-ip6.c xio-ipapp.c xio-tcp.c \
	xio-sctp.c xio-dccp.c xio-rawip.c xio-posixmq.c \
	xio-socks.c xio-socks5.c xio-proxy.c xio-udp.c xio-udplite.c \
	xio-progcall.c xio-exec.c xio-system.c xio-shell.c \
	xio-termios.c xio-readline.c \
	xio-pty.c xio-openssl.c xio-namespaces.c \
	xio-ascii.c xiolockfile.c xio-tcpwrap.c xio-fs.c xio-tun.c

UTLSRCS = error.c dalan.c fdname.c sysutils.c utils.c nestlex.c vsnprintf_r.c snprinterr.c filan.c sycls.c sslcls.c

OBJS = socat.o $(XIOSRCS:.c=.o) $(UTLSRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	$(STRIP) --strip-all $@

install: $(TARGET)
	cp $(TARGET) $(MODULE_BIN)/$(TARGET)
	chmod 755 $(MODULE_BIN)/$(TARGET)
	rm -f $(TARGET) *.o

clean:
	rm -f $(TARGET) *.o libxio.a

.PHONY: all install clean
