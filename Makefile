# VERSION = $(shell git describe --tags)

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-D_GNU_SOURCE \
	${CFLAGS}
	# -DENVOY_VERSION=\"${VERSION}\" \

LDLIBS = -larchive -lalpm -lgpgme

all: repoman
repoman: repoman.o alpm_metadata.o archive_reader.o pkghash.o buffer.o signing.o base64.o

install: repoman
	install -Dm755 repoman ${DESTDIR}/usr/bin/repoman
	# install -Dm644 repoman.1 $(DESTDIR)/usr/share/man/man1/repoman.1

clean:
	${RM} repoman *.o

.PHONY: clean install uninstall
