## repose

An archlinux repo poking tool.

At the moment, a half baked and hacked together alternative to repo-add.

### Usage

    repose -U --sign foo.db.tar.gz

Add all packages in the current directory to foo.db.tar.gz and then sign
the database with gnupg.

Add a `-c` flag to also delete and files removed from the db and a
second `-c` to also remove any other outdated packages in the cache.

    repose -V foo.db.tar.gz

Verify the packages foo.db.tar.gz refers to can be accessed and the
md5sum and sha256sum match.

    repose -R foo.db.tar.gz pkg ...

Remove a package from the database. Add `-c` to also remove the files
from disk.

    repose -Q foo.db.tar.gz pkg ...

Print information about `pkg` much like how `pacman -Qi pkg` works

### Compatability

Backwards compatibility to the original repo tools is provided. Simply
make symlinks against `repose`

    ln -s /usr/bin/repose /usr/bin/repo-add
    ln -s /usr/bin/repose /usr/bin/repo-remove
    ln -s /usr/bin/repose /usr/bin/repo-elephant

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
