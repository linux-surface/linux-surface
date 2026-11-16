CFLAGS ?= -O2 -g
override CFLAGS += -Wall -Wextra -std=gnu11
PREFIX ?= /usr/local
VERSION := $(shell cat VERSION)
# Manual installs go to /etc; distro packages pass /usr/lib paths.
SYSTEMDUNITDIR ?= /etc/systemd/system
MODPROBEDIR ?= /etc/modprobe.d
MODULESLOADDIR ?= /etc/modules-load.d
WIREPLUMBERDIR ?= $(PREFIX)/share/wireplumber/wireplumber.conf.d
DOCDIR ?= $(PREFIX)/share/doc/surface-camd
DISTDIR ?= $(abspath ../build)

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
	install -D -m 0644 surface-camd@.service $(DESTDIR)$(SYSTEMDUNITDIR)/surface-camd@.service
	sed -i 's|@BINDIR@|$(PREFIX)/bin|' $(DESTDIR)$(SYSTEMDUNITDIR)/surface-camd@.service
	install -D -m 0644 v4l2loopback-surface.conf $(DESTDIR)$(MODPROBEDIR)/v4l2loopback-surface.conf
	install -D -m 0644 ../modprobe.d/surface-ipu3-cameras.conf $(DESTDIR)$(MODPROBEDIR)/surface-ipu3-cameras.conf
	install -D -m 0644 v4l2loopback-load.conf $(DESTDIR)$(MODULESLOADDIR)/v4l2loopback-surface.conf
	install -D -m 0644 ../wireplumber/51-surface-ipu3-cameras.conf $(DESTDIR)$(WIREPLUMBERDIR)/51-surface-ipu3-cameras.conf
	install -D -m 0644 ../wireplumber/52-surface-camd-only.conf $(DESTDIR)$(DOCDIR)/52-surface-camd-only.conf
	install -D -m 0644 README.md $(DESTDIR)$(DOCDIR)/README.md

# Source tarball for distro packages (includes ../modprobe.d and ../wireplumber).
dist:
	mkdir -p $(DISTDIR)
	tar -C .. -czf $(DISTDIR)/surface-camd-$(VERSION).tar.gz --transform 's|^|surface-camd-$(VERSION)/|' \
		--exclude='surface-camd/surface-camd' --exclude='surface-camd/test-unpack' \
		--exclude='surface-camd/test-process' --exclude='*.tar.gz' \
		surface-camd modprobe.d wireplumber

clean:
	rm -f surface-camd test-unpack test-process

.PHONY: all test install dist clean
