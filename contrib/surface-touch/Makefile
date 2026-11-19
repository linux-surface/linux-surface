CFLAGS ?= -O2 -g
override CFLAGS += -Wall -Wextra -std=gnu11 -DVERSION='"$(VERSION)"'
PREFIX ?= /usr/local
VERSION := $(shell cat VERSION)
SYSTEMDUNITDIR ?= /etc/systemd/system
CONFDIR ?= /etc
PYTHONDIR ?= $(PREFIX)/lib/surface-touch/python
DOCDIR ?= $(PREFIX)/share/doc/surface-touch
DISTDIR ?= $(abspath build)

SRC = src/engine.c src/json.c src/keys.c
HDR = src/engine.h src/json.h src/keys.h

all: surface-touchd surface-touchctl

surface-touchd: src/surface-touchd.c $(SRC) $(HDR) VERSION
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/surface-touchd.c $(SRC) $(LDFLAGS) -lm

surface-touchctl: src/surface-touchctl.c
	$(CC) $(CFLAGS) $(CPPFLAGS) -o $@ src/surface-touchctl.c $(LDFLAGS)

test-engine: tests/test-engine.c src/engine.c src/engine.h
	$(CC) $(CFLAGS) -o $@ tests/test-engine.c src/engine.c -lm

test-json-keys: tests/test-json-keys.c src/json.c src/keys.c src/json.h src/keys.h
	$(CC) $(CFLAGS) -o $@ tests/test-json-keys.c src/json.c src/keys.c

test: test-engine test-json-keys surface-touchd
	./test-engine
	./test-json-keys
	tests/test-replay.sh ./surface-touchd

install: all
	install -D -m 0755 surface-touchd $(DESTDIR)$(PREFIX)/bin/surface-touchd
	install -D -m 0755 surface-touchctl $(DESTDIR)$(PREFIX)/bin/surface-touchctl
	install -D -m 0644 surface-touchd.service $(DESTDIR)$(SYSTEMDUNITDIR)/surface-touchd.service
	sed -i 's|@BINDIR@|$(PREFIX)/bin|' $(DESTDIR)$(SYSTEMDUNITDIR)/surface-touchd.service
	install -D -m 0644 python/surface_touch.py $(DESTDIR)$(PYTHONDIR)/surface_touch.py
	install -D -m 0644 README.md $(DESTDIR)$(DOCDIR)/README.md
	install -D -m 0644 surface-touchd.conf $(DESTDIR)$(DOCDIR)/surface-touchd.conf.example
	[ -e $(DESTDIR)$(CONFDIR)/surface-touchd.conf ] || \
		install -D -m 0644 surface-touchd.conf $(DESTDIR)$(CONFDIR)/surface-touchd.conf

# Source tarball for distro packages.
dist:
	mkdir -p $(DISTDIR)
	tar -C .. -czf $(DISTDIR)/surface-touch-$(VERSION).tar.gz \
		--transform 's|^surface-touch|surface-touch-$(VERSION)|' \
		--exclude='surface-touch/build' --exclude='surface-touch/surface-touchd' \
		--exclude='surface-touch/surface-touchctl' --exclude='surface-touch/test-engine' \
		--exclude='surface-touch/test-json-keys' --exclude='__pycache__' \
		surface-touch

clean:
	rm -f surface-touchd surface-touchctl test-engine test-json-keys

.PHONY: all test install dist clean
