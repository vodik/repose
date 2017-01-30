use std::any::Any;
use std::path::{Path, PathBuf, Component};
use std::io::Read;
use std::cmp::Ordering;
use std::iter::Iterator;
use std::collections::HashMap;
use std::collections::hash_map::{self, Values};
use time;
use libarchive::archive::{ReadFilter, WriteFilter, ReadFormat, WriteFormat, Entry, FileType};
use libarchive::reader::{self, Reader};
use libarchive::writer::{self, Writer, WriterEntry};
use storage::Storage;
use package::{self, Package, Metadata};
use desc;
use error::{Error, Result};
use std::fs::Permissions;
use std::os::unix::fs::PermissionsExt;
use std::os::unix::io::IntoRawFd;
use crypto::digest::Digest;
use crypto::sha2::Sha256;

pub struct Repo {
    cache: HashMap<String, Package>,
}

fn parse_entry(name: &str) -> Option<(&str, &str)> {
    name.rfind("-").and_then(|dash1| {
        name[..dash1].rfind("-").map(|dash2| {
            (&name[..dash2], &name[dash2 + 1..])
        })
    })
}

fn get_pkg_metadata(path: &str) -> Option<(&str, &str)> {
    let components: Vec<_> = Path::new(path)
        .components()
        .collect();

    match components[..] {
        [Component::Normal(pkg), Component::Normal(_)] => pkg.to_str().and_then(parse_entry),
        _ => None,
    }
}

fn populate_entry(entry: &mut WriterEntry,
                  dir: &PathBuf,
                  filetype: FileType,
                  mode: Permissions)
                  -> Result<()> {
    let now = time::now().to_timespec();

    entry.set_pathname(dir);
    entry.set_filetype(filetype);
    entry.set_perm(mode);
    entry.set_uname("repose");
    entry.set_gname("repose");
    entry.set_ctime(&now);
    entry.set_mtime(&now);
    entry.set_atime(&now);
    Ok(())
}

fn commit_entry(archive: &Writer,
                entry: &mut WriterEntry,
                folder: &str,
                name: &str,
                data: &str)
                -> Result<()> {
    let mut path = PathBuf::from(folder);
    path.push(name);
    populate_entry(entry,
                   &path,
                   FileType::RegularFile,
                   Permissions::from_mode(0o600))?;
    entry.set_size(data.len() as i64);

    archive.write_header(&entry)?;
    archive.write_data(data.as_bytes())?;
    entry.clear();
    Ok(())
}

fn header(data: &package::Entry) -> Option<&'static str> {
    match *data {
        package::Entry::Filename => Some("%FILENAME%\n"),
        package::Entry::Name => Some("%NAME%\n"),
        package::Entry::Base => Some("%BASE%\n"),
        package::Entry::Version => Some("%VERSION%\n"),
        package::Entry::Description => Some("%DESC%\n"),
        package::Entry::Groups => Some("%GROUP%\n"),
        package::Entry::PackageSize => Some("%CSIZE%\n"),
        package::Entry::InstallSize => Some("%ISIZE%\n"),
        package::Entry::PGPSig => Some("%PGPSIG%\n"),
        package::Entry::SHA256Sum => Some("%SHA256SUM%\n"),
        package::Entry::Url => Some("%URL%\n"),
        package::Entry::License => Some("%LICENSE%\n"),
        package::Entry::Arch => Some("%ARCH%\n"),
        package::Entry::BuildDate => Some("%BUILDDATE%\n"),
        package::Entry::Packager => Some("%PACKAGER%\n"),
        package::Entry::Replaces => Some("%REPLACES%\n"),
        package::Entry::Depends => Some("%DEPENDS%\n"),
        package::Entry::Conflicts => Some("%CONFLICTS%\n"),
        package::Entry::Provides => Some("%PROVIDES%\n"),
        package::Entry::OptDepends => Some("%OPTDEPENDS%\n"),
        package::Entry::MakeDepends => Some("%MAKEDEPENDS%\n"),
        package::Entry::CheckDepends => Some("%CHECKDEPENDS%\n"),
        package::Entry::Files => Some("%FILES%\n"),
        package::Entry::Deltas => Some("%DELTAS%\n"),
        _ => None,
    }
}

fn write_meta(data: (&package::Entry, &Metadata)) -> String {
    let mut buf = header(data.0).map(String::from).unwrap();

    match *data.1 {
        Metadata::Text(ref entry) => {
            buf.push_str(&entry);
            buf.push('\n');
        }
        Metadata::Size(ref entry) => {
            buf.push_str(&entry.to_string());
            buf.push('\n');
        }
        Metadata::Timestamp(ref entry) => {
            buf.push_str(&entry.to_string());
            buf.push('\n');
        }
        Metadata::List(ref entry) => {
            for line in entry {
                buf.push_str(&line);
                buf.push('\n');
            }
        }
        Metadata::Path(ref entry) => {
            buf.push_str(&entry.as_os_str().to_string_lossy());
            buf.push('\n');
        }
    }
    buf.push('\n');
    buf
}

fn pkg_meta(pkg: &Package, entry: &package::Entry) -> Option<String> {
    pkg.metadata.get(entry).map(|value| write_meta((entry, value)))
}

pub enum RepoEntry {
    Desc,
    Depends,
    Files,
}

pub struct Desc;
pub struct Depends;
pub struct Files;

trait DatabaseEntry {
    fn filename(&self) -> &'static str;
    fn render(&self, pkg: &Package, store: &Storage) -> String;
}

impl DatabaseEntry for RepoEntry {
    // TODO: this needs to be rethought, wow what a mess...
    fn filename(&self) -> &'static str {
        match *self {
            RepoEntry::Desc => Desc.filename(),
            RepoEntry::Depends => Depends.filename(),
            RepoEntry::Files => Files.filename(),
        }
    }

    fn render(&self, pkg: &Package, store: &Storage) -> String {
        match *self {
            RepoEntry::Desc => Desc.render(pkg, store),
            RepoEntry::Depends => Depends.render(pkg, store),
            RepoEntry::Files => Files.render(pkg, store),
        }
    }
}

impl DatabaseEntry for Desc {
    fn filename(&self) -> &'static str {
        "desc"
    }

    fn render(&self, pkg: &Package, store: &Storage) -> String {
        let mut page = String::new();
        let filename = pkg.metadata.get(&package::Entry::Filename).unwrap();

        page.push_str(&write_meta((&package::Entry::Filename, &filename)));
        pkg_meta(pkg, &package::Entry::Name).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Base).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Version).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Description).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Groups).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::PackageSize).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::InstallSize).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::PGPSig).map(|data| page.push_str(&data));

        match pkg.metadata.get(&package::Entry::SHA256Sum) {
            Some(sha256sum) => {
                page.push_str(&write_meta((&package::Entry::SHA256Sum, &sha256sum)));
            }
            None => {
                if let &Metadata::Path(ref filename) = filename {
                    println!("computing sha256sum for {}...", pkg.name);
                    let mut data = [0; 0x2000];
                    let mut sha256 = Sha256::new();

                    let mut file = store.open(filename).unwrap();
                    loop {
                        let len = file.read(&mut data).unwrap();
                        if len == 0 {
                            break;
                        }
                        sha256.input(&data[..len]);
                    }

                    let digest = Metadata::Text(sha256.result_str());
                    page.push_str(&write_meta((&package::Entry::SHA256Sum, &digest)));
                }
            }
        };

        pkg_meta(pkg, &package::Entry::Url).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::License).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Arch).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::BuildDate).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Packager).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Replaces).map(|data| page.push_str(&data));
        page
    }
}

impl DatabaseEntry for Depends {
    fn filename(&self) -> &'static str {
        "depends"
    }

    #[allow(unused)]
    fn render(&self, pkg: &Package, store: &Storage) -> String {
        let mut page = String::new();
        pkg_meta(pkg, &package::Entry::Depends).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Conflicts).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::Provides).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::OptDepends).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::MakeDepends).map(|data| page.push_str(&data));
        pkg_meta(pkg, &package::Entry::CheckDepends).map(|data| page.push_str(&data));
        page
    }
}

impl DatabaseEntry for Files {
    fn filename(&self) -> &'static str {
        "files"
    }

    fn render(&self, pkg: &Package, store: &Storage) -> String {
        let mut page = String::new();
        match pkg.metadata.get(&package::Entry::Files) {
            Some(files) => {
                page.push_str(&write_meta((&package::Entry::Files, &files)));
            }
            None => {
                let filename = pkg.metadata.get(&package::Entry::Filename).unwrap();
                if let &Metadata::Path(ref filename) = filename {
                    println!("computing file list for {}...", pkg.name);

                    let mut builder = reader::Builder::new();
                    builder.support_format(ReadFormat::All).unwrap();
                    builder.support_filter(ReadFilter::All).unwrap();

                    let file = store.open(filename).unwrap();
                    let mut reader = builder.open_stream(file).unwrap();
                    let mut files: Vec<String> = Vec::new();
                    loop {
                        match reader.next_header() {
                            Some(entry) => {
                                let pathname = entry.pathname();

                                if !pathname.starts_with(".") {
                                    files.push(pathname.into());
                                }
                            }
                            None => break,
                        }
                    }

                    page.push_str(&write_meta((&package::Entry::Files, &Metadata::List(files))));
                }
            }
        }
        page
    }
}

impl Repo {
    pub fn new() -> Self {
        Repo { cache: HashMap::new() }
    }

    pub fn load<T>(&mut self, src: T) -> Result<()>
        where T: Any + Read
    {
        let mut builder = reader::Builder::new();
        builder.support_format(ReadFormat::All)?;
        builder.support_filter(ReadFilter::All)?;

        let mut reader = builder.open_stream(src)?;
        loop {
            let pkg = {
                let entry = reader.next_header();
                if entry.is_none() {
                    break;
                }

                entry.and_then(|entry| match entry.filetype() {
                        FileType::RegularFile => get_pkg_metadata(entry.pathname()),
                        _ => None,
                    })
                    .map(|(pkgname, pkgver)| {
                        self.cache
                            .entry(pkgname.into())
                            .or_insert_with(|| Package::new(pkgname, pkgver))
                    })
            };

            match pkg {
                Some(mut pkg) => {
                    // Quick hack to get this done and move on. The
                    // problem is long file lists span multiple blocks
                    // and dealing with that efficiently isn't
                    // straight forward. Copy everything ahead of
                    // time, then parse.
                    //
                    // Might be possible to do less copying by using a
                    // custom provider, or doing something fancy, but
                    // it also might make sense to drop nom.
                    //
                    // After all, I still need to make a copy at the
                    // end of the day...
                    let mut page: Vec<u8> = Vec::with_capacity(4098);

                    loop {
                        let block = reader.read_block().unwrap();
                        match block {
                            Some(value) => page.extend(value),
                            None => break,
                        }
                    }

                    let mut parser = desc::Parser::new();
                    parser.read(pkg, &page);
                }
                None => continue,
            }
        }
        Ok(())
    }

    pub fn from_iter<I>(iter: I) -> Self
        where I: Iterator<Item = Package>
    {
        let mut repo = Repo::new();
        for pkg in iter {
            match repo.cache.entry(pkg.name.clone()) {
                hash_map::Entry::Occupied(mut o) => {
                    match o.get().version.cmp(&pkg.version) {
                        Ordering::Less => {
                            o.insert(pkg);
                        }
                        _ => {}
                    };
                }
                hash_map::Entry::Vacant(v_) => {
                    v_.insert(pkg);
                }
            }
        }

        repo
    }

    pub fn pkgs(&self) -> Values<String, Package> {
        self.cache.values()
    }

    pub fn get(&self, pkgname: &str) -> Option<&Package> {
        self.cache.get(pkgname)
    }

    pub fn entry(&mut self, pkgname: String) -> hash_map::Entry<String, Package> {
        self.cache.entry(pkgname)
    }

    pub fn save<T>(&self, dst: T, store: &Storage, entries: &[RepoEntry]) -> Result<()>
        where T: IntoRawFd
    {
        let mut builder = writer::Builder::new();
        builder.set_format(WriteFormat::PaxRestricted)?;
        builder.add_filter(WriteFilter::None)?;

        let writer = builder.open_stream(dst)?;
        let mut entry = WriterEntry::new();

        populate_entry(&mut entry,
                       &PathBuf::from("/"),
                       FileType::Directory,
                       Permissions::from_mode(0o700))?;
        writer.write_header(&entry)?;
        entry.clear();


        for pkg in self.pkgs() {
            populate_entry(&mut entry,
                           &PathBuf::from("/").join(&pkg.to_string()),
                           FileType::Directory,
                           Permissions::from_mode(0o700))?;
            writer.write_header(&entry)?;
            entry.clear();

            for repoentry in entries {
                let page = repoentry.render(pkg, store);
                if !page.is_empty() {
                    commit_entry(&writer,
                                 &mut entry,
                                 &pkg.to_string(),
                                 repoentry.filename(),
                                 &page)?;
                }
            }
        }

        Ok(())
    }
}

impl Storage {
    pub fn iter_pkgs(&self) -> Result<impl Iterator<Item = Package>> {
        Ok(self.read_dir()?.filter_map(|entry| {
            entry.ok()
                .map(|entry| entry.path())
                .and_then(|filename| match Package::load(filename) {
                    Ok(pkg) => Some(pkg),
                    Err(Error::MissingPKGINFO) => None,
                    Err(x) => panic!(x),
                })
        }))
    }
}
