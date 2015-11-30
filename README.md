## repose

<a href="https://travis-ci.org/vodik/repose">
  <img alt="Travis CI Status"
       src="https://travis-ci.org/vodik/repose.png"/>
</a>
<a href="https://scan.coverity.com/projects/3577">
  <img alt="Coverity Scan Build Status"
       src="https://scan.coverity.com/projects/3577/badge.svg"/>
</a>

Owning more than one Archlinux machine, I operate my own repository to
distribute customized and/or extra packages between the various
machines. `repo-add`, the provided tool for repository management, is
frustratingly limited. Updating the repository after building a series
of packages quickly turned into a slow monstrous bash script: either I
had to have rather complex logic to figure out which packages are new,
or I had to do the expensive operation of rebuilding the repository each
time. Surly, though, this was something that could be automated.

`repose` is an Archlinux repository compiler.

Generally, it operates by building up two package caches: one that
represents the contents of the database and another that represents the
various packages sitting in the root directory of the database.
Updating, then, is simply a sync operation operation between the two.

To sync, it takes advantage of several rules of Archlinux repositories
to automate as much logic as possible:

1. Repositories typically only hold one version of a package (and we're
   interesting in the newest version).
2. Repositories typically only hold only one architecture.
3. Repositories and packages are expected to be in the same directory.

### Updating/Removing

Most simplistically:

    repose -z foo

1. Parse the contents of `foo.db` if it exists.
2. If we find a new package in the database's folder, add it to the
   database.
3. If we can't find a corresponding package to a database entry, remove
   it from the database.
4. Write out an updated database.

To explicitly remove a package:

    repose -zd foo [pkgs]

Removes the specified packages from the database.

To generate a complementary `foo.files` file, add the `-f` flags

    repose -zf foo

### Globbing

Its possible to use globbing. `repose` uses the following logic for
finding/filtering packages:

1. Does it match the package's filename
2. Does it match the package's name
3. Does it glob pkgname-pkgver

This allows for operations like:

Add latest detected version `systemd` to a repo:

    repose foo systemd

Add a specific version of `systemd` to a repo:

    repose foo systemd-209-1

Drop all git packages from the repo:

    repose -zd foo '*-git-*'

```
     __
    '. \
     '- \
      / /_         .---.
     / | \\,.\/--.//    )
     |  \//        )/  /
      \  ' ^ ^    /    )____.----..  6
       '.____.    .___/            \._)
          .\/.                      )
           '\                       /
           _/ \/    ).        )    (
          /#  .!    |        /\    /
          \  C// #  /'-----''/ #  /
       .   'C/ |    |    |   |    |mrf  ,
       \), .. .'OOO-'. ..'OOO'OOO-'. ..\(,
```
