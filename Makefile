VERSION=6.1
GIT_DESC=$(shell test -d .git && git describe 2>/dev/null)

ifneq "$(GIT_DESC)" ""
VERSION=$(GIT_DESC)
endif

CFLAGS := -std=c11 -g \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-Wno-missing-field-initializers \
	-D_GNU_SOURCE \
	-D_FILE_OFFSET_BITS=64 \
	-DREPOSE_VERSION=\"$(VERSION)\" \
	$(CFLAGS)

VPATH = src
LDLIBS = -larchive -lalpm -lgpgme -lcrypto
PREFIX = /usr

all: repose
repose: repose.o database.o package.o file.o util.o filecache.o \
	pkghash.o buffer.o base64.o filters.o signing.o \
	reader.o desc.o

librepose.so: util.c
	$(LINK.o) $(CFLAGS) -fPIC -shared $^ -o $@

tests: librepose.so
	@py.test -v tests

install: repose
	install -Dm755 repose $(DESTDIR)$(PREFIX)/bin/repose
	install -Dm644 _repose $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_repose
	install -Dm644 man/repose.1 $(DESTDIR)$(PREFIX)/share/man/man1/repose.1

clean:
	$(RM) repose librepose.so *.o

.PHONY: tests clean install uninstall
