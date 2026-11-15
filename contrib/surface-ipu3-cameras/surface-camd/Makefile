CFLAGS ?= -O2 -g
CFLAGS += -Wall -Wextra -std=gnu11
PREFIX ?= /usr/local

all: surface-camd

surface-camd: surface-camd.c ipu3-unpack.h
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ surface-camd.c $(LDFLAGS) -lm

test-unpack: test-unpack.c ipu3-unpack.h
	$(CC) $(CFLAGS) -o $@ test-unpack.c

test-process: test-process.c surface-camd.c ipu3-unpack.h
	$(CC) $(CFLAGS) -Wno-unused-function -o $@ test-process.c -lm

test: test-unpack test-process
	./test-unpack
	./test-process

install: surface-camd
	install -D -m 0755 surface-camd $(DESTDIR)$(PREFIX)/bin/surface-camd
	install -D -m 0644 surface-camd@.service $(DESTDIR)/etc/systemd/system/surface-camd@.service
	install -D -m 0644 v4l2loopback-surface.conf $(DESTDIR)/etc/modprobe.d/v4l2loopback-surface.conf
	install -D -m 0644 v4l2loopback-load.conf $(DESTDIR)/etc/modules-load.d/v4l2loopback-surface.conf
	sed -i 's|@BINDIR@|$(PREFIX)/bin|' $(DESTDIR)/etc/systemd/system/surface-camd@.service

clean:
	rm -f surface-camd test-unpack test-process

.PHONY: all test install clean
