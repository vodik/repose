use std::path::PathBuf;
use std::collections::HashMap;
use glob::Pattern;
use package::{Package, Entry, Metadata};


pub struct Filter {
    string: String,
    pattern: Pattern,
}

impl Filter {
    pub fn new(s: &str) -> Option<Self> {
        Pattern::new(s).ok().map(|pattern| {
            Filter {
                pattern: pattern,
                string: s.to_string(),
            }
        })
    }

    pub fn match_pkg(&self, pkg: &Package) -> bool {
        if pkg.name == self.string {
            return true;
        }

        let filepath = pkg.metadata
            .get(&Entry::Filename)
            .and_then(|filename| match *filename {
                Metadata::Path(ref path) => Some(path.as_os_str().to_string_lossy()),
                _ => None,
            });

        match filepath {
            Some(path) => path == self.string || self.pattern.matches(&path),
            None => false,
        }
    }
}

pub fn match_targets(pkg: &Package, targets: &[Filter]) -> bool {
    for filter in targets {
        if filter.match_pkg(pkg) {
            return true;
        }
    }

    false
}

#[test]
fn test_match_name() {
    let mut metadata: HashMap<Entry, Metadata> = HashMap::new();
    metadata.insert(Entry::Filename, PathBuf::from("test-1.pkg.tar.xz").into());

    let pkg = Package {
        name: "test".into(),
        version: "1".into(),
        arch: "any".into(),
        metadata: metadata,
    };

    let filter = Filter::new("test").unwrap();
    assert_eq!(filter.match_pkg(&pkg), true);

    let filter = Filter::new("foobar").unwrap();
    assert_eq!(filter.match_pkg(&pkg), false);
}

#[test]
fn test_filename() {
    let mut metadata: HashMap<Entry, Metadata> = HashMap::new();
    metadata.insert(Entry::Filename, PathBuf::from("test-1.pkg.tar.xz").into());

    let pkg = Package {
        name: "test".into(),
        version: "1".into(),
        arch: "any".into(),
        metadata: metadata,
    };

    let filter = Filter::new("test-1.pkg.tar.xz").unwrap();
    assert_eq!(filter.match_pkg(&pkg), true);

    let filter = Filter::new("test-2.pkg.tar.xz").unwrap();
    assert_eq!(filter.match_pkg(&pkg), false);
}

#[test]
fn test_glob() {
    let mut metadata: HashMap<Entry, Metadata> = HashMap::new();
    metadata.insert(Entry::Filename, PathBuf::from("test-1.pkg.tar.xz").into());

    let pkg = Package {
        name: "test".into(),
        version: "1".into(),
        arch: "any".into(),
        metadata: metadata,
    };

    let filter = Filter::new("test*").unwrap();
    assert_eq!(filter.match_pkg(&pkg), true);

    let filter = Filter::new("*-1*").unwrap();
    assert_eq!(filter.match_pkg(&pkg), true);

    let filter = Filter::new("*foobar*").unwrap();
    assert_eq!(filter.match_pkg(&pkg), false);
}
