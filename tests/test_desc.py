import pytest


REPOSE_DESC = b'''%FILENAME%
repose-git-5.19.g82c3d4a-1-x86_64.pkg.tar.xz

%NAME%
repose-git

%VERSION%
5.19.g82c3d4a-1

%DESC%
A archlinux repo building tool

%CSIZE%
18804

%ISIZE%
51200

%SHA256SUM%
4045b3b24bae8a2d811323e5dd3727345e9e6f81788c65d5935d07b2ee06b505

%URL%
http://github.com/vodik/repose

%LICENSE%
GPL

%ARCH%
x86_64

%BUILDDATE%
1448690669

%PACKAGER%
Simon Gomizelj <simongmzlj@gmail.com>
'''


REPOSE_DEPENDS = b'''%DEPENDS%
pacman
libarchive
gnupg

%CONFLICTS%
repose

%PROVIDES%
repose

%MAKEDEPENDS%
git
'''


def new_pkg(ffi, name, version):
    name = ffi.new('char[]', name)
    version = ffi.new('char[]', version)
    pkg = ffi.new('struct pkg*', {'name': name,
                                  'version': version})

    pytest.weakkeydict[pkg] = (name, version)
    return pkg


def read_alpm_string_list(ffi, data):
    def worker(data):
        while data != ffi.NULL:
            yield ffi.string(ffi.cast('char*', data.data))
            data = data.next
    return list(worker(data))


@pytest.fixture
def parser(ffi, lib):
    class Parser(object):
        def __init__(self, ffi):
            self.save = b''
            self.parser = ffi.new('struct desc_parser*')
            lib.desc_parser_init(self.parser)

        def feed(self, pkg, data):
            data = self.save + data
            data_len = len(data)
            result = lib.desc_parser_feed(self.parser, pkg, data, data_len)
            self.save = data[result:]

        @property
        def entry(self):
            return self.parser.entry

    return Parser(ffi)


@pytest.fixture
def pkg(ffi):
    return new_pkg(ffi, b'repose-git', b'5.19.g82c3d4a-1')


def test_parse_desc(ffi, lib, pkg, parser):
    parser.feed(pkg, REPOSE_DESC)
    assert parser.entry == lib.PKG_PACKAGER

    assert pkg.base == ffi.NULL
    assert pkg.base64sig == ffi.NULL
    assert ffi.string(pkg.filename) == b'repose-git-5.19.g82c3d4a-1-x86_64.pkg.tar.xz'
    assert ffi.string(pkg.desc) == b'A archlinux repo building tool'
    assert pkg.size == 18804
    assert pkg.isize == 51200
    assert ffi.string(pkg.sha256sum) == b'4045b3b24bae8a2d811323e5dd3727345e9e6f81788c65d5935d07b2ee06b505'
    assert ffi.string(pkg.url) == b'http://github.com/vodik/repose'
    assert ffi.string(pkg.arch) == b'x86_64'
    assert pkg.builddate == 1448690669
    assert ffi.string(pkg.packager) == b'Simon Gomizelj <simongmzlj@gmail.com>'

    licenses = read_alpm_string_list(ffi, pkg.licenses)
    assert licenses == [b'GPL']


def test_parse_depends(ffi, lib, pkg, parser):
    parser.feed(pkg, REPOSE_DEPENDS)
    assert parser.entry == lib.PKG_MAKEDEPENDS

    depends = read_alpm_string_list(ffi, pkg.depends)
    assert depends == [b'pacman', b'libarchive', b'gnupg']

    conflicts = read_alpm_string_list(ffi, pkg.conflicts)
    assert conflicts == [b'repose']

    provides = read_alpm_string_list(ffi, pkg.provides)
    assert provides == [b'repose']

    makedepends = read_alpm_string_list(ffi, pkg.makedepends)
    assert makedepends == [b'git']


@pytest.mark.parametrize("chunksize", [1, 10, 100])
def test_parse_chunked(ffi, lib, pkg, parser, chunksize):
    def chunk(data, size):
        return (data[i:i+size] for i in range(0, len(data), size))

    for chunk in chunk(REPOSE_DESC, chunksize):
        parser.feed(pkg, chunk)

    assert pkg.base == ffi.NULL
    assert pkg.base64sig == ffi.NULL
    assert ffi.string(pkg.filename) == b'repose-git-5.19.g82c3d4a-1-x86_64.pkg.tar.xz'
    assert ffi.string(pkg.desc) == b'A archlinux repo building tool'
    assert pkg.size == 18804
    assert pkg.isize == 51200
    assert ffi.string(pkg.sha256sum) == b'4045b3b24bae8a2d811323e5dd3727345e9e6f81788c65d5935d07b2ee06b505'
    assert ffi.string(pkg.url) == b'http://github.com/vodik/repose'
    assert ffi.string(pkg.arch) == b'x86_64'
    assert pkg.builddate == 1448690669
    assert ffi.string(pkg.packager) == b'Simon Gomizelj <simongmzlj@gmail.com>'

    licenses = read_alpm_string_list(ffi, pkg.licenses)
    assert licenses == [b'GPL']
