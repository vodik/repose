use std::fmt::Debug;
use std::ops::Deref;
use std::str::from_utf8;
use std::collections::hash_map;
use nom::{IResult, newline};
use package::{Package, Entry, Metadata};
use error::Result;

pub struct Parser {
    entry: Option<Entry>,
}

#[derive(Debug)]
enum Line<'a> {
    Entry(Entry),
    Line(&'a str),
    Seperator,
}


named!(header<Line>, do_parse!(
    entry: alt!(
        tag!("%FILENAME%")     => {|_| Line::Entry(Entry::Filename)}
      | tag!("%NAME%")         => {|_| Line::Entry(Entry::Name)}
      | tag!("%BASE%")         => {|_| Line::Entry(Entry::Base)}
      | tag!("%VERSION%")      => {|_| Line::Entry(Entry::Version)}
      | tag!("%DESC%")         => {|_| Line::Entry(Entry::Description)}
      | tag!("%GROUP%")        => {|_| Line::Entry(Entry::Groups)}
      | tag!("%CSIZE%")        => {|_| Line::Entry(Entry::PackageSize)}
      | tag!("%ISIZE%")        => {|_| Line::Entry(Entry::InstallSize)}
      | tag!("%SHA256SUM%")    => {|_| Line::Entry(Entry::SHA256Sum)}
      | tag!("%PGPSIG%")       => {|_| Line::Entry(Entry::PGPSig)}
      | tag!("%URL%")          => {|_| Line::Entry(Entry::Url)}
      | tag!("%LICENSE%")      => {|_| Line::Entry(Entry::License)}
      | tag!("%ARCH%")         => {|_| Line::Entry(Entry::Arch)}
      | tag!("%BUILDDATE%")    => {|_| Line::Entry(Entry::BuildDate)}
      | tag!("%PACKAGER%")     => {|_| Line::Entry(Entry::Packager)}
      | tag!("%REPLACES%")     => {|_| Line::Entry(Entry::Replaces)}
      | tag!("%DEPENDS%")      => {|_| Line::Entry(Entry::Depends)}
      | tag!("%CONFLICTS%")    => {|_| Line::Entry(Entry::Conflicts)}
      | tag!("%PROVIDES%")     => {|_| Line::Entry(Entry::Provides)}
      | tag!("%OPTDEPENDS%")   => {|_| Line::Entry(Entry::OptDepends)}
      | tag!("%MAKEDEPENDS%")  => {|_| Line::Entry(Entry::MakeDepends)}
      | tag!("%CHECKDEPENDS%") => {|_| Line::Entry(Entry::CheckDepends)}
      | tag!("%FILES%")        => {|_| Line::Entry(Entry::Files)}
      | tag!("%DELTAS%")       => {|_| Line::Entry(Entry::Deltas)}
    ) >>
    newline >>
    (entry)
));

named!(value<Line>, map_res!(
    take_until_and_consume!("\n"),
    |str| from_utf8(str).map(Line::Line)
));

named!(desc_entry<Line>, do_parse!(
    entry: alt!(do_parse!(newline >> (Line::Seperator)) | header | value) >>
    (entry)
));

pub trait DescCallbacks {
    fn on_name(&mut self, value: &str);
    fn on_version(&mut self, value: &str);
    fn on_arch(&mut self, value: &str);
    fn on_metadata(&mut self, entry: &Entry, value: &str);
}

impl DescCallbacks for Package {
    fn on_name(&mut self, value: &str) {
        assert_eq!(self.name, value);
    }

    fn on_version(&mut self, value: &str) {
        assert_eq!(self.version.deref(), value);
    }

    fn on_arch(&mut self, value: &str) {
        self.arch = value.into();
    }

    fn on_metadata(&mut self, entry: &Entry, value: &str) {
        let metadata = self.metadata.entry(entry.clone());
        match metadata {
            hash_map::Entry::Occupied(mut o) => {
                match *o.get_mut() {
                    Metadata::List(ref mut l) => l.push(value.into()),
                    _ => panic!("shouldn't happen but TODO"),
                };
            }
            hash_map::Entry::Vacant(v) => {
                v.insert((entry, value).into());
            }
        };
    }
}

impl Parser {
    pub fn new() -> Self {
        Parser { entry: None }
    }

    pub fn read<T>(&mut self, cbs: &mut T, b: &[u8]) -> Option<()>
        where T: DescCallbacks + Debug
    {
        let mut data = b;

        loop {
            if let IResult::Done(rest, result) = desc_entry(data) {
                match result {
                    Line::Entry(entry) => self.entry = Some(entry),
                    Line::Line(line) => {
                        match self.entry {
                            Some(Entry::Name) => cbs.on_name(line),
                            Some(Entry::Version) => cbs.on_version(line),
                            Some(Entry::Arch) => cbs.on_arch(line),
                            Some(ref entry) => cbs.on_metadata(&entry, line),
                            None => panic!("invalid state!"),
                        }
                    }
                    Line::Seperator => self.entry = None,
                }

                data = rest;
            } else {
                return None;
            }
        }
        Some(())
    }
}
