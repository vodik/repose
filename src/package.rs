use std::fmt;
use std::fs;
use std::ops::Deref;
use std::cmp::{PartialOrd, Ordering};
use std::path::{Path, PathBuf, Component};
use std::collections::HashMap;
use std::os::unix::fs::MetadataExt;
// TODO: Fix imports
use libarchive::archive::{ReadFilter, ReadFormat, Entry as _Entry};
use libarchive::reader::{self, Reader};
use error::{Result, Error};
use alpm;

#[derive(Clone, Hash, PartialEq, Eq, Debug)]
pub enum Entry {
    Filename,
    Name,
    Base,
    Version,
    Description,
    Groups,
    PackageSize,
    InstallSize,
    SHA256Sum,
    PGPSig,
    Url,
    License,
    Arch,
    BuildDate,
    Packager,
    Replaces,
    Depends,
    Conflicts,
    Provides,
    OptDepends,
    MakeDepends,
    CheckDepends,
    Files,
    Deltas,
    Backups,
    BuildOptions,
    BuildDirectory,
    BuildEnvironment,
    BuildInstalled,
}

#[derive(Debug, Clone, PartialEq, Eq)]
pub struct Version(String);

impl Version {
    pub fn new<S>(s: S) -> Self
        where S: Into<String>
    {
        Version(s.into())
    }
}

impl fmt::Display for Version {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl<'a> From<&'a str> for Version {
    fn from(s: &str) -> Self {
        Version::new(s)
    }
}

impl Deref for Version {
    type Target = String;

    fn deref(&self) -> &String {
        &self.0
    }
}

impl Ord for Version {
    fn cmp(&self, other: &Version) -> Ordering {
        // TODO: this presence of this line has a measurable
        // performance impact
        alpm::pkg_vercmp(&self.0, &other.0).unwrap()
        // Ordering::Less
    }
}

impl PartialOrd for Version {
    fn partial_cmp(&self, other: &Version) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum Arch {
    Any,
    Arch(String),
    Unknown,
}

impl Default for Arch {
    fn default() -> Self {
        Arch::Unknown
    }
}

impl<'a> From<&'a str> for Arch {
    fn from(arch: &str) -> Self {
        match arch {
            "any" => Arch::Any,
            _ => Arch::Arch(arch.into()),
        }
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum Metadata {
    Text(String),
    Size(u64),
    Timestamp(i64),
    List(Vec<String>),
    Path(PathBuf),
}

impl<'a> From<&'a [&'a str]> for Metadata {
    fn from(values: &[&str]) -> Self {
        Metadata::List(values.iter().map(|&x| x.to_owned()).collect())
    }
}

impl<'a> From<&'a str> for Metadata {
    fn from(value: &str) -> Self {
        Metadata::Text(value.into())
    }
}

impl From<u64> for Metadata {
    fn from(size: u64) -> Self {
        Metadata::Size(size)
    }
}

impl From<i64> for Metadata {
    fn from(ts: i64) -> Self {
        Metadata::Timestamp(ts)
    }
}

impl From<PathBuf> for Metadata {
    fn from(path: PathBuf) -> Self {
        Metadata::Path(path.into())
    }
}

// TODO this is not quite appropriate
impl<'a> From<(&'a Entry, &'a Vec<&'a str>)> for Metadata {
    fn from((entry, values): (&'a Entry, &'a Vec<&'a str>)) -> Self {
        match *entry {
            Entry::Filename => PathBuf::from(values[0]).into(),
            Entry::Base => values[0].into(),
            Entry::Description => values[0].into(),
            Entry::PackageSize => values[0].parse::<u64>().unwrap().into(),
            Entry::InstallSize => values[0].parse::<u64>().unwrap().into(),
            Entry::SHA256Sum => values[0].into(),
            Entry::PGPSig => values[0].into(),
            Entry::Url => values[0].into(),
            Entry::BuildDate => values[0].parse::<i64>().unwrap().into(),
            Entry::Packager => values[0].into(),
            // TODO: Not all entries are extended metadata
            _ => values.as_slice().into(),
        }
    }
}

impl<'a> From<(&'a Entry, &'a str)> for Metadata {
    fn from((entry, value): (&'a Entry, &'a str)) -> Self {
        match *entry {
            Entry::Filename => PathBuf::from(value).into(),
            Entry::Base => value.into(),
            Entry::Description => value.into(),
            Entry::PackageSize => value.parse::<u64>().unwrap().into(),
            Entry::InstallSize => value.parse::<u64>().unwrap().into(),
            Entry::SHA256Sum => value.into(),
            Entry::PGPSig => value.into(),
            Entry::Url => value.into(),
            Entry::BuildDate => value.parse::<i64>().unwrap().into(),
            Entry::Packager => value.into(),
            // TODO: Not all entries are extended metadata
            _ => [value][..].into(),
        }
    }
}

#[derive(Debug, Eq, PartialEq, Clone)]
pub struct Package {
    pub name: String,
    pub version: Version,
    pub arch: Arch,
    pub metadata: HashMap<Entry, Metadata>,
}

impl Package {
    pub fn new(pkgname: &str, pkgver: &str) -> Self {
        Package {
            name: pkgname.into(),
            version: pkgver.into(),
            arch: Default::default(),
            metadata: HashMap::new(),
        }
    }

    pub fn load<T>(filename: T) -> Result<Self>
        where T: AsRef<Path>
    {
        let metadata = fs::metadata(filename.as_ref()).unwrap();
        let basename = {
            let path = filename.as_ref();
            if let Component::Normal(basename) = path.components().rev().next().unwrap() {
                PathBuf::from(basename)
            } else {
                panic!("unexpected filepath") // TODO: better error
            }
        };

        let mut builder = reader::Builder::new();
        builder.support_format(ReadFormat::All)?;
        builder.support_filter(ReadFilter::All)?;

        let mut reader = builder.open_file(filename)?;
        let pkg = loop {
            let found = {
                match reader.next_header() {
                    None => break Err(Error::MissingPKGINFO),
                    Some(entry) => entry.pathname() == ".PKGINFO",
                }
            };

            if found {
                // TODO: loop and error handling
                break reader.read_block()?
                    .and_then(Package::pkginfo)
                    .ok_or(Error::MalformedPKGINFO);
            };
        };

        pkg.map(|mut pkg| {
            let entry = Metadata::Path(basename);
            pkg.metadata.insert(Entry::PackageSize, metadata.size().into());
            pkg.metadata.insert(Entry::Filename, entry);
            pkg
        })
    }
}

impl Ord for Package {
    fn cmp(&self, other: &Package) -> Ordering {
        self.name.cmp(&other.name).then_with(|| self.version.cmp(&other.version))
    }
}

impl PartialOrd for Package {
    fn partial_cmp(&self, other: &Package) -> Option<Ordering> {
        Some(self.cmp(other))
    }
}

impl fmt::Display for Package {
    fn fmt(&self, f: &mut fmt::Formatter) -> fmt::Result {
        write!(f, "{}-{}", self.name, self.version)
    }
}

#[test]
fn test_version_sort() {
    let mut res: Vec<Package> = ["1.4", "1.4.1", "1.4-1", "1.5-1", "1.5", "1.3-1", "1.3.1-1"]
        .iter()
        .map(From::from)
        .collect();

    res.sort();
    assert_eq!(res,
               &[Version::new("1.3-1"),
                 Version::new("1.3.1-1"),
                 Version::new("1.4"),
                 Version::new("1.4-1"),
                 Version::new("1.4.1"),
                 Version::new("1.5-1"),
                 Version::new("1.5")]);
}
