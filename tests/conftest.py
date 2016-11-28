import pytest
import cffi


CFLAGS = ['-std=c11', '-O0', '-g', '-D_GNU_SOURCE']
SOURCES = ['../src/desc.c', '../src/pkginfo.c',
           '../src/package.c', '../src/pkgcache.c',
           '../src/util.c', '../src/base64.c']


def pytest_configure(config):
    ffi = cffi.FFI()
    with open('tests/_repose.h') as header:
        header = ffi.set_source('repose',
                                header.read(),
                                include_dirs=['../src'],
                                libraries=['archive', 'alpm'],
                                sources=SOURCES,
                                extra_compile_args=CFLAGS)

    with open('tests/_repose.c') as cdef:
        ffi.cdef(cdef.read())

    ffi.compile(tmpdir='tests')


@pytest.fixture
def size_t_max():
    from repose import lib
    return lib.SIZE_MAX
