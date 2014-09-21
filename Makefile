VERSION=2
GIT_DESC=$(shell test -d .git && git describe 2>/dev/null)

ifneq "$(GIT_DESC)" ""
VERSION=$(GIT_DESC)
endif

CFLAGS := -std=c11 \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-D_GNU_SOURCE \
	-DREPOSE_VERSION=\"$(VERSION)\" \
	$(CFLAGS)

VPATH = src
LDLIBS = -larchive -lalpm -lgpgme -lcrypto -lssl
PREFIX = /usr

all: repose
repose: repose.o database.o package.o file.o util.o filecache.o \
	pkghash.o strbuf.o base64.o filters.o signing.o \
	reader.o desc.o

install: repose
	install -Dm755 repose $(DESTDIR)$(PREFIX)/bin/repose
	install -Dm644 _repose $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_repose
	install -Dm644 man/repose.1 $(DESTDIR)$(PREFIX)/share/man/man1/repose.1

clean:
	$(RM) repose *.o

.PHONY: clean install uninstall
