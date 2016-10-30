import pytest
import weakref
import cffi
import importlib


def pytest_namespace():
    ffi = cffi.FFI()
    with open('tests/_repose.h') as header:
        header = ffi.set_source('repose', header.read(),
                                include_dirs=['../src'],
                                sources=['../src/desc.c', '../src/pkginfo.c',
                                         '../src/package.c', '../src/pkghash.c',
                                         '../src/util.c', '../src/base64.c'],
                                extra_compile_args=['-std=c11', '-O0', '-g', '-D_GNU_SOURCE'],
                                libraries=['archive', 'alpm'])

    with open('tests/_repose.c') as cdef:
        ffi.cdef(cdef.read())

    ffi.compile(tmpdir='tests')
    return {'weakkeydict': weakref.WeakKeyDictionary(),
            'repose': importlib.import_module('repose')}


@pytest.fixture
def size_t_max(lib):
    return lib.SIZE_MAX


@pytest.fixture
def ffi():
    return pytest.repose.ffi


@pytest.fixture
def lib():
    return pytest.repose.lib
