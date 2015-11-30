import pytest
import cffi


def pytest_namespace():
    return {'FFI': cffi.FFI}


@pytest.fixture(scope='session')
def climits():
    ffi = pytest.FFI()
    ffi.cdef("const size_t size_t_max;")
    return ffi.verify("const size_t size_t_max = (size_t)-1;")


@pytest.fixture(scope='session')
def size_t_max(climits):
    return climits.size_t_max


@pytest.fixture(scope='session')
def ffi():
    ffi = pytest.FFI()
    ffi.cdef("""
typedef long long time_t;

struct config {
    int verbose;
    int compression;
    bool reflink;
    bool sign;
    char *arch;
};

extern struct config config;

char *joinstring(const char *root, ...);
int str_to_size(const char *str, size_t *out);
int str_to_time(const char *size, time_t *out);
char *strstrip(char *s);
""")
    return ffi


@pytest.fixture(scope='session')
def lib(ffi):
    return ffi.dlopen("./librepose.so")
