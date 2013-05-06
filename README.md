## repoman

An archlinux repo poking tool.

At the moment, a half baked and hacked together alternative to repo-add.

### Usage

    repoman -Ur foo .

Add all packages in the current directory to foo.db.tar.gz

    repoman -Vr foo

Verify the packages foo.db.tar.gz refers to can be accessed and the
md5sum and sha256sum match.
