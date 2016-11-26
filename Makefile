DOT = dot
RAGEL = ragel
RAGEL_FLAGS := -G2

COMPILE.rl = $(RAGEL) $(RAGEL_FLAGS)
COMPILE.dot = $(DOT) $(DOT_FLAGS)

%.c: %.rl
	$(COMPILE.rl) -C $(OUTPUT_OPTION) $<

%.dot: %.rl
	$(COMPILE.rl) -Vp $(OUTPUT_OPTION) $<

%.png: %.dot
	$(COMPILE.dot) -Tpng $(OUTPUT_OPTION) $<

VERSION=7
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

PYTEST_FLAGS := --boxed $(PYTEST_FLAGS)

VPATH = src
LDLIBS = -larchive -lalpm -lgpgme -lcrypto
PREFIX = /usr

all: repose
desc.o: $(VPATH)/desc.c
desc.dot: $(VPATH)/desc.rl
pkginfo.o: $(VPATH)/pkginfo.c
pkginfo.dot: $(VPATH)/pkginfo.rl

repose: repose.o database.o package.o util.o filecache.o \
	pkghash.o buffer.o base64.o filters.o signing.o \
	pkginfo.o desc.o

tests: desc.c pkginfo.c
	py.test tests $(PYTEST_FLAGS)

graphs: desc.png pkginfo.dot

install: repose
	install -Dm755 repose $(DESTDIR)$(PREFIX)/bin/repose
	install -Dm644 _repose $(DESTDIR)$(PREFIX)/share/zsh/site-functions/_repose
	install -Dm644 man/repose.1 $(DESTDIR)$(PREFIX)/share/man/man1/repose.1

clean:
	$(RM) repose $(VPATH)/desc.c $(VPATH)/pkginfo.c *.o *.dot *.png

.PHONY: tests clean graph install uninstall
