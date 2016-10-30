import pytest
from datetime import datetime
from repose import lib, ffi
from wrappers import Parser, Package


REPOSE_DESC = '''%FILENAME%
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


REPOSE_DEPENDS = '''%DEPENDS%
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


class DescParser(Parser):
    def init_parser(self):
        parser = ffi.new('struct desc_parser*')
        lib.desc_parser_init(parser)
        return parser

    def feed_parser(self, parser, pkg, data):
        return lib.desc_parser_feed(parser, pkg, data, len(data))


@pytest.fixture
def parser():
    return DescParser()


@pytest.fixture
def pkg():
    return Package(name='repose-git', version='5.19.g82c3d4a-1')


def test_parse_desc(pkg, parser):
    parser.feed(pkg, REPOSE_DESC)
    assert parser.entry == lib.PKG_PACKAGER

    assert pkg.base is None
    assert pkg.base64sig is None
    assert pkg.filename == 'repose-git-5.19.g82c3d4a-1-x86_64.pkg.tar.xz'
    assert pkg.desc == 'A archlinux repo building tool'
    assert pkg.size == 18804
    assert pkg.isize == 51200
    assert pkg.sha256sum == '4045b3b24bae8a2d811323e5dd3727345e9e6f81788c65d5935d07b2ee06b505'
    assert pkg.url == 'http://github.com/vodik/repose'
    assert pkg.arch == 'x86_64'
    assert pkg.builddate == "Nov 28, 2015, 01:04:29"
    assert pkg.packager == 'Simon Gomizelj <simongmzlj@gmail.com>'
    assert pkg.licenses == ['GPL']


def test_parse_depends(pkg, parser):
    parser.feed(pkg, REPOSE_DEPENDS)
    assert parser.entry == lib.PKG_MAKEDEPENDS

    assert pkg.depends == ['pacman', 'libarchive', 'gnupg']
    assert pkg.conflicts == ['repose']
    assert pkg.provides == ['repose']
    assert pkg.makedepends == ['git']


@pytest.mark.parametrize('chunksize', [1, 10, 100])
def test_parse_chunked(pkg, parser, chunksize):
    def chunk(data, size):
        return (data[i:i+size] for i in range(0, len(data), size))

    for chunk in chunk(REPOSE_DESC, chunksize):
        parser.feed(pkg, chunk)

    assert pkg.base is None
    assert pkg.base64sig is None
    assert pkg.filename == 'repose-git-5.19.g82c3d4a-1-x86_64.pkg.tar.xz'
    assert pkg.desc == 'A archlinux repo building tool'
    assert pkg.size == 18804
    assert pkg.isize == 51200
    assert pkg.sha256sum == '4045b3b24bae8a2d811323e5dd3727345e9e6f81788c65d5935d07b2ee06b505'
    assert pkg.url == 'http://github.com/vodik/repose'
    assert pkg.arch == 'x86_64'
    assert pkg.builddate == "Nov 28, 2015, 01:04:29"
    assert pkg.packager == 'Simon Gomizelj <simongmzlj@gmail.com>'
    assert pkg.licenses == ['GPL']
