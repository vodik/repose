use std::io;
use std::result;
use std::error;
use std::fmt;
use std::ffi;
use libarchive::error::ArchiveError;

pub type Result<T> = result::Result<T, Error>;

#[derive(Debug)]
pub enum Error {
    MissingPKGINFO,
    MalformedPKGINFO,
    Archive(ArchiveError),
    Io(io::Error),
    Nul(ffi::NulError),
}

impl error::Error for Error {
    fn description(&self) -> &str {
        match *self {
            Error::MissingPKGINFO => "archive missing .PKGINFO",
            Error::MalformedPKGINFO => "archive malformed .PKGINFO",
            Error::Archive(ref err) => err.description(),
            Error::Io(ref err) => err.description(),
            Error::Nul(ref err) => err.description(),
        }
    }

    fn cause(&self) -> Option<&error::Error> {
        match *self {
            Error::Archive(ref err) => Some(err),
            Error::Io(ref err) => Some(err),
            Error::Nul(ref err) => Some(err),
            _ => None,
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, fmt: &mut fmt::Formatter) -> fmt::Result {
        match *self {
            Error::MissingPKGINFO => write!(fmt, "archive missing .PKGINFO"),
            Error::MalformedPKGINFO => write!(fmt, "archive malformed .PKGINFO"),
            Error::Archive(ref err) => write!(fmt, "{}", err),
            Error::Io(ref err) => write!(fmt, "{}", err),
            Error::Nul(ref err) => write!(fmt, "{}", err),
        }
    }
}

impl From<ArchiveError> for Error {
    fn from(err: ArchiveError) -> Error {
        Error::Archive(err)
    }
}

impl From<io::Error> for Error {
    fn from(err: io::Error) -> Error {
        Error::Io(err)
    }
}

impl From<ffi::NulError> for Error {
    fn from(err: ffi::NulError) -> Error {
        Error::Nul(err)
    }
}
