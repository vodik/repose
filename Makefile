VERSION=0.1
GIT_DESC=$(shell test -d .git && git describe 2>/dev/null)

ifneq "$(GIT_DESC)" ""
VERSION=$(GIT_DESC)
endif

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-D_GNU_SOURCE \
	-DREPOSE_VERSION=\"$(VERSION)\" \
	$(CFLAGS)

LDLIBS = -larchive -lalpm -lgpgme -lcrypto -lssl
PREFIX = /usr

all: repose
repose: repose.o database.o package.o memblock.o util.o files.o \
	pkghash.o strbuf.o base64.o \
	reader.o desc.o

# parsers/desc.c: parsers/desc.rl
# 	ragel -T1 -C $< -o $@

install: repose
	install -Dm755 repose $(DESTDIR)$(PREFIX)/bin/repose
	install -Dm644 _repose $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_repose
	# install -Dm644 repose.1 $(DESTDIR)/usr/share/man/man1/repose.1

clean:
	$(RM) repose *.o parsers/*.o

.PHONY: clean install uninstall
