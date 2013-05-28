# VERSION = $(shell git describe --tags)

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-D_GNU_SOURCE \
	${CFLAGS}
	# -DENVOY_VERSION=\"${VERSION}\" \

LDLIBS = -larchive -lalpm -lgpgme -lcrypto -lssl

all: repoman
repoman: repoman.o buffer.o alpm/alpm_metadata.o alpm/archive_reader.o \
	alpm/pkghash.o alpm/signing.o alpm/base64.o alpm/util.o

install: repoman
	install -Dm755 repoman ${DESTDIR}/usr/bin/repoman
	# install -Dm644 repoman.1 $(DESTDIR)/usr/share/man/man1/repoman.1

clean:
	${RM} repoman *.o alpm/*.o

.PHONY: clean install uninstall
