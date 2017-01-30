use libc::{self, c_int};
use std::io::Error;
use std::path::{Path, PathBuf};
use std::fs::{File, ReadDir, read_dir};
use std::ffi::CString;
use std::os::unix::io::*;
use std::os::unix::ffi::OsStrExt;
use package::{Package, Entry, Metadata};
use error::Result;

pub struct Storage {
    root: PathBuf,
    fd: RawFd,
}

macro_rules! posix {
    ($expr:expr) => (match $expr {
        Err(err) => Err(From::from(err)),
        Ok(-1) => Err(Error::last_os_error().into()),
        Ok(ok) => Ok(ok),
    })
}

fn opendir(path: &Path) -> Result<RawFd> {
    posix!(CString::new(path.as_os_str().as_bytes())
        .map(|buf| unsafe { libc::open(buf.as_ptr(), libc::O_RDONLY | libc::O_DIRECTORY) }))
}

fn openat(fd: RawFd, path: &Path) -> Result<RawFd> {
    posix!(CString::new(path.as_os_str().as_bytes())
        .map(|buf| unsafe { libc::openat(fd, buf.as_ptr(), libc::O_RDONLY) }))
}

fn createat(fd: RawFd, path: &Path) -> Result<RawFd> {
    posix!(CString::new(path.as_os_str().as_bytes()).map(|buf| unsafe {
        libc::openat(fd,
                     buf.as_ptr(),
                     libc::O_CREAT | libc::O_WRONLY | libc::O_TRUNC,
                     0o644)
    }))
}

fn faccessat(fd: RawFd, path: &Path, a_mode: c_int, flag: c_int) -> Result<c_int> {
    posix!(CString::new(path.as_os_str().as_bytes())
        .map(|buf| unsafe { libc::faccessat(fd, buf.as_ptr(), a_mode, flag) }))
}

impl Storage {
    pub fn new<P>(root: P) -> Result<Self>
        where P: AsRef<Path>
    {
        let root = root.as_ref();
        opendir(root).map(|fd| {
            Storage {
                root: root.into(),
                fd: fd,
            }
        })
    }

    pub fn open<P>(&self, file: P) -> Result<File>
        where P: AsRef<Path>
    {
        openat(self.fd, file.as_ref()).map(|fd| unsafe { File::from_raw_fd(fd) })
    }

    pub fn create<P>(&self, file: P) -> Result<File>
        where P: AsRef<Path>
    {
        createat(self.fd, file.as_ref()).map(|fd| unsafe { File::from_raw_fd(fd) })
    }

    pub fn access<P>(&self, file: P) -> Result<bool>
        where P: AsRef<Path>
    {
        faccessat(self.fd, file.as_ref(), libc::F_OK, 0)?;
        Ok(true)
    }

    pub fn read_dir(&self) -> Result<ReadDir> {
        read_dir(&self.root).map_err(From::from)
    }
}

impl Drop for Storage {
    fn drop(&mut self) {
        unsafe {
            libc::close(self.fd);
        }
    }
}

impl Package {
    pub fn present(&self, store: &Storage) -> Option<bool> {
        self.metadata
            .get(&Entry::Filename)
            .map(|filename| match *filename {
                Metadata::Path(ref filename) => store.access(filename).unwrap_or(false),
                _ => panic!("Mismatched filename metadata"),
            })
    }
}
