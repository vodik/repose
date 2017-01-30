#![feature(slice_patterns)]
#![feature(loop_break_value)]
#![feature(ordering_chaining)]
#![feature(conservative_impl_trait)]

extern crate base64;
extern crate crypto;
extern crate getopts;
extern crate glob;
extern crate libarchive;
extern crate libc;
#[macro_use]
extern crate nom;
extern crate rand;
extern crate time;
extern crate uname;

mod alpm;
mod filters;
mod storage;
mod database;
mod desc;
mod package;
mod pkginfo;
mod error;
mod elephant;

use std::env;
use std::cmp::Ordering;
use std::collections::hash_map::Entry;
use getopts::Options;
use uname::uname;
use storage::Storage;
use database::{Repo, RepoEntry};
use elephant::elephant;
use filters::{Filter, match_targets};
use package::Arch;
use error::Result;

macro_rules! ignore {
    ($expr:expr) => (match $expr {
        Err(_) => (),
        Ok(_) => (),
    })
}

fn system_arch() -> Result<Arch> {
    let machine: &str = &uname()?.machine;
    Ok(machine.into())
}

fn print_usage(program: &str, opts: Options) {
    let brief = format!("usage: {} [options] <database> [pkgs ...]", program);
    print!("{}", opts.usage(&brief));
}

fn load_repo(repo: &mut Repo, name: &str, root: &Storage) -> Result<()> {
    ignore!(root.open(format!("{}.db", name))
        .and_then(|db| repo.load(db)));
    ignore!(root.open(format!("{}.files", name))
        .and_then(|files| repo.load(files)));
    Ok(())
}

fn save_repo(name: &str, root: &Storage, repo: &mut Repo) -> Result<()> {
    let reponame = format!("{}.db", name);
    println!("writing {}...", reponame);
    repo.save(root.create(reponame)?,
              root,
              &[RepoEntry::Desc, RepoEntry::Depends])?;

    let reponame = format!("{}.files", name);
    println!("writing {}...", reponame);
    repo.save(root.create(reponame)?, root, &[RepoEntry::Files])?;
    Ok(())
}

fn load_filters(args: &[String]) -> Option<Vec<Filter>> {
    if args.is_empty() {
        return None;
    }

    let mut filters = Vec::new();
    for arg in args {
        filters.push(Filter::new(arg).unwrap());
    }
    Some(filters)
}

fn main() {
    let args: Vec<String> = env::args().collect();
    let program = args[0].clone();

    let mut opts = Options::new();
    opts.optflag("h", "help", "display this help and exit");
    opts.optflag("V", "version", "display version");
    opts.optflag("v", "verbose", "verbose output");
    opts.optflag("f", "files", "build the files database");
    opts.optflag("l", "list", "list packages in the repository");
    opts.optopt("r", "root", "set the root for the repository", "PATH");
    opts.optopt("p", "pool", "set the pool to find packages in", "PATH");
    opts.optopt("m", "arch", "the architecture of the database", "ARCH");
    opts.optflag("s", "sign", "create a database signature");
    opts.optflag("j", "bzip2", "filter the archive through bzip2");
    opts.optflag("J", "xz", "filter the archive through xz");
    opts.optflag("z", "gzip", "filter the archive through gzip");
    opts.optflag("Z", "compress", "filter the archive through compress");
    opts.optflag("", "reflink", "use reflinks instead of symlinks");
    opts.optflag("", "rebuild", "force rebuild the repository");
    opts.optflag("", "elephant", "print an elephant");

    let matches = match opts.parse(&args[1..]) {
        Ok(m) => m,
        Err(f) => {
            print!("{}\n", f);
            std::process::exit(1);
        }
    };

    if matches.opt_present("help") {
        print_usage(&program, opts);
    } else if matches.opt_present("elephant") {
        println!("{}", elephant().unwrap());
    } else {
        if matches.free.is_empty() {
            panic!("missing repository name");
        }

        let name = matches.free.get(0).unwrap();
        let filters = load_filters(&matches.free[1..]);

        let target_arch = matches.opt_str("arch")
            .map(Arch::Arch)
            .unwrap_or_else(|| system_arch().unwrap());

        let root_dir = matches.opt_str("root").unwrap_or(".".into());
        let root = Storage::new(root_dir).unwrap();
        let mut repo = Repo::new();

        if !matches.opt_present("rebuild") {
            load_repo(&mut repo, name, &root).unwrap();
        }

        if matches.opt_present("list") {
            let mut pkgs: Vec<_> = repo.pkgs().collect();
            pkgs.sort_by(|&a, &b| a.name.cmp(&b.name));
            for pkg in pkgs {
                println!("{}", pkg);
            }
        } else {
            let filtered = repo.pkgs().filter_map(|pkg| match pkg.present(&root) {
                Some(true) => Some(pkg),
                Some(false) => {
                    println!("dropping {}", pkg);
                    None
                }
                None => panic!("Missing filename metadata on package"),
            });

            let mut repo = Repo::from_iter(filtered.cloned());
            for pkg in root.iter_pkgs().unwrap() {
                match filters {
                    Some(ref filters) => {
                        if !match_targets(&pkg, &filters) {
                            continue;
                        }
                    }
                    None => {}
                };

                if pkg.arch != target_arch && pkg.arch != Arch::Any {
                    continue;
                }

                match repo.entry(pkg.name.clone()) {
                    Entry::Occupied(mut o) => {
                        match o.get().version.cmp(&pkg.version) {
                            Ordering::Less => {
                                println!("updating {} -> {}", o.get(), pkg.version);
                                o.insert(pkg);
                            }
                            _ => {}
                        };
                    }
                    Entry::Vacant(v_) => {
                        println!("adding {}", pkg);
                        v_.insert(pkg);
                    }
                }
            }

            save_repo(name, &root, &mut repo).unwrap();
        }
    }
}
