# VERSION = $(shell git describe --tags)

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-D_GNU_SOURCE \
	${CFLAGS}
	# -DENVOY_VERSION=\"${VERSION}\" \

LDLIBS = -larchive -lalpm

all: repoman
repoman: repoman.o alpm-simple.o archive_extra.o hashtable.o buffer.o

install: repoman
	install -Dm755 repoman ${DESTDIR}/usr/bin/repoman
	# install -Dm644 repoman.1 $(DESTDIR)/usr/share/man/man1/repoman.1

clean:
	${RM} repoman *.o

.PHONY: clean install uninstall
