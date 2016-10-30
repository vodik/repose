import abc
import pytest
from repose import ffi


class marshal_int(object):
    def __init__(self, field):
        self.field = field

    def __get__(self, obj, cls):
        return getattr(obj._struct, self.field)


class marshal_date(object):
    def __init__(self, field):
        self.field = field

    def __get__(self, obj, cls):
        return getattr(obj._struct, self.field)


class marshal_string(object):
    def __init__(self, field):
        self.field = field

    def __get__(self, obj, cls):
        attr = getattr(obj._struct, self.field)
        if attr == ffi.NULL:
            return None
        return ffi.string(attr).decode()


class marshal_string_list(object):
    def __init__(self, field):
        self.field = field

    def __get__(self, obj, cls):
        def marshal_list(node):
            while node != ffi.NULL:
                yield ffi.string(ffi.cast('char*', node.data)).decode()
                node = node.next

        attr = getattr(obj._struct, self.field)
        return list(marshal_list(attr))


class Package(object):
    def __init__(self, name=None, version=None):
        init_data = {}

        if name:
            init_data['name'] = ffi.new('char[]', name.encode())
        if version:
            init_data['version'] = ffi.new('char[]', version.encode())

        self._struct = ffi.new('struct pkg*', init_data)
        pytest.weakkeydict[self._struct] = tuple(init_data.values())

    arch = marshal_string('arch')
    base = marshal_string('base')
    base64sig = marshal_string('base64sig')
    builddate = marshal_date('builddate')
    checkdepends = marshal_string_list('checkdepends')
    conflicts = marshal_string_list('conflicts')
    depends = marshal_string_list('depends')
    desc = marshal_string('desc')
    filename = marshal_string('filename')
    isize = marshal_int('isize')
    licenses = marshal_string_list('licenses')
    makedepends = marshal_string_list('makedepends')
    optdepends = marshal_string_list('optdepends')
    packager = marshal_string('packager')
    provides = marshal_string_list('provides')
    sha256sum = marshal_string('sha256sum')
    size = marshal_int('size')
    url = marshal_string('url')


class Parser(object):
    __metaclass__ = abc.ABCMeta

    def __init__(self):
        self._saved = b''
        self.parser = self.init_parser()
        if not self.parser:
            raise RuntimeError('Parser failed to initialize properly')

    def feed(self, pkg, data):
        data = self._saved + data.encode()
        result = self.feed_parser(self.parser, pkg._struct, data)
        self._saved = data[result:]

    @property
    def entry(self):
        return self.parser.entry

    @abc.abstractmethod
    def init_parser(self):
        return

    @abc.abstractmethod
    def feed_parser(self, parser, pkg, data):
        return
