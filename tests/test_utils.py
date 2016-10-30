import pytest
import errno
from repose import ffi, lib


@pytest.mark.parametrize("input", [
    (b'foo ', b'bar ', b'baz'),
    (b'Hello ', b'', b'World', b'!')
])
def test_joinstring(input):
    args = [ffi.new("char[]", x) for x in input]
    args.append(ffi.NULL)

    result = lib.joinstring(*args)
    assert ffi.string(result) == b''.join(input)


@pytest.mark.parametrize("input", [
    b'Hello World',
    b'Hello World  ',
    b'    Hello World',
    b'\tHello World   '
])
def test_strstrip(input):
    arg = ffi.new("char[]", input)

    result = lib.strstrip(arg)
    assert ffi.string(result) == input.strip()


def test_parse_size():
    arg = ffi.new("char[]", b"832421")
    out = ffi.new("size_t *")

    assert lib.parse_size(arg, out) == 0
    assert out[0] == 832421


def test_parse_size_ERANGE(size_t_max):
    input = str(size_t_max + 1).encode('utf-8')
    arg = ffi.new("char[]", input)
    out = ffi.new("size_t *")

    assert lib.parse_size(arg, out) == -1
    assert ffi.errno == errno.ERANGE
    assert out[0] == 0


def test_parse_time():
    arg = ffi.new("char[]", b"1448690669")
    out = ffi.new("time_t *")

    assert lib.parse_time(arg, out) == 0
    assert out[0] == 1448690669
