# SPDX-License-Identifier: MIT
prefix ?= /usr/local
bindir ?= $(prefix)/bin
libexecdir ?= $(prefix)/libexec
userunitdir ?= $(prefix)/lib/systemd/user
pkgdir = $(libexecdir)/surface-laptop1-ir
DESTDIR ?=
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
PW_CFLAGS := $(shell $(PKG_CONFIG) --cflags libpipewire-0.3)
PW_LIBS := $(shell $(PKG_CONFIG) --libs libpipewire-0.3)
INSTALL ?= install
PYTHON ?= python3

.PHONY: all check clean install uninstall
all: surface-ir

surface-ir: surface-ir.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -std=gnu11 -Wall -Wextra $(PW_CFLAGS) $(LDFLAGS) $< -o $@ $(PW_LIBS)

test-pairs: test-pairs.c surface-ir.c
	$(CC) $(CPPFLAGS) $(CFLAGS) -UNDEBUG -std=gnu11 -Wall -Wextra $(PW_CFLAGS) $(LDFLAGS) $< -o $@ $(PW_LIBS)

check: test-pairs
	./test-pairs
	PYTHONDONTWRITEBYTECODE=1 $(PYTHON) test-discovery.py

install: surface-ir
	$(INSTALL) -d "$(DESTDIR)$(bindir)" "$(DESTDIR)$(pkgdir)" "$(DESTDIR)$(userunitdir)"
	$(INSTALL) -m 0755 surface-ir surface-laptop1-ir.py "$(DESTDIR)$(pkgdir)/"
	$(INSTALL) -m 0644 LICENSE "$(DESTDIR)$(pkgdir)/"
	ln -sfn "$$(realpath -m --relative-to="$(bindir)" "$(pkgdir)/surface-laptop1-ir.py")" "$(DESTDIR)$(bindir)/surface-laptop1-ir"
	$(PYTHON) render-unit.py "$(bindir)/surface-laptop1-ir" "$(DESTDIR)$(userunitdir)/surface-laptop1-ir.service"

uninstall:
	$(RM) "$(DESTDIR)$(bindir)/surface-laptop1-ir" "$(DESTDIR)$(userunitdir)/surface-laptop1-ir.service"
	$(RM) "$(DESTDIR)$(pkgdir)/surface-ir" "$(DESTDIR)$(pkgdir)/surface-laptop1-ir.py" "$(DESTDIR)$(pkgdir)/LICENSE"
	-rmdir "$(DESTDIR)$(pkgdir)"

clean:
	$(RM) surface-ir test-pairs
