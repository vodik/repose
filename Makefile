VERSION = $(shell git describe --tags)

CFLAGS := -std=c99 \
	-Wall -Wextra -pedantic \
	-Wshadow -Wpointer-arith -Wcast-qual -Wstrict-prototypes -Wmissing-prototypes \
	-D_GNU_SOURCE \
	-DREPOSE_VERSION=\"${VERSION}\" \
	${CFLAGS}

LDLIBS = -larchive -lalpm -lgpgme -lcrypto -lssl
PREFIX = /usr

all: repose
repose: repose.o database.o strbuf.o utils.o \
	alpm/alpm_metadata.o alpm/archive_reader.o \
	alpm/pkghash.o alpm/signing.o alpm/base64.o alpm/util.o

install: repose
	install -Dm755 repose ${DESTDIR}${PREFIX}/bin/repose
	install -Dm644 _repose ${DESTDIR}${PREFIX}/share/zsh/site-functions/_repose
	# install -Dm644 repose.1 $(DESTDIR)/usr/share/man/man1/repose.1

clean:
	${RM} repose *.o alpm/*.o

.PHONY: clean install uninstall
