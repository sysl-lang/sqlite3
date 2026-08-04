# sqlite3

A [sysl](https://github.com/edadma/sysl) binding for SQLite.

The module is `sqlite`, and the directory it lives in is what names it — a module is a directory and
its name is that directory's path relative to the package root. The C that sits beside it is part of
the library: sysl compiles it as part of the build, so nothing here needs a makefile and there is no
build script anywhere in this repository.

```
sqlite/
    sqlite.sysl     the binding
    consts.c        the constants SQLite defines as macros, which a header cannot export
package.hocon       who this package is, and what it needs of the machine
```

## Using it

Build the package into an artifact once, then compile against it:

```
sysl build-lib . -o /tmp/sqlite.syslib
sysl run yourprogram.sysl --lib /tmp/sqlite.syslib
```

**Nothing on the consuming side mentions `-lsqlite3`.** The `link "sqlite3"` directive lives in this
library's own header and travels inside the artifact, so a program that links is a program that did
not have to know what this binds.

You will need SQLite's development files on the machine — `libsqlite3` and its header — since this is
a binding and not a copy.

## What it binds

Two handles, both of them C's incomplete types: `sqlite3` and `sqlite3_stmt` are declared and never
defined in SQLite's header, so no size was ever on offer. They are written here as `opaque struct`,
which says exactly that — `*Sqlite3` and `*Stmt` get types of their own, and neither can be crossed
with the other or with the `*u8` strings declared beside them.

Almost nothing else crosses the boundary that is not a pointer or an integer: no struct to lay out,
no callback to install, no varargs. That is what makes SQLite a good first binding.

`NULL` is the case a binding has to have an answer for, and it has one: reading a column that holds
SQL NULL answers `Ok(None)`, which is not the same answer as a decode failure and does not
dereference the null pointer SQLite hands back.

## Example

```sysl
import sqlite.*

// An in-memory database, so the program leaves nothing behind and needs no writable directory. The
// name is SQLite's own: it is not a path.
open(":memory:") match
    Ok(db) ->
        run(db)
        db.close()

    Err(why) -> print(f"cannot open the database: ${why}")

run(db: Db)
    setup(db) match
        Err(why) -> print(f"setup failed: ${why}")
        Ok(_) ->
            insert(db, "Ada Lovelace", 1815)
            insert(db, "Alan Turing", 1912)
            insert(db, "Grace Hopper", 1906)

            report(db)
end run

setup(db: Db) -> Result[bool, string] = db.exec("create table people (name text, born integer)")

// One prepared statement, bound and run once. `bind_*` numbers parameters from 1, which is SQLite's
// numbering rather than this binding's -- the first `?` is 1.
insert(db: Db, name: string, born: int)
    db.prepare("insert into people (name, born) values (?, ?)") match
        Err(why) -> print(f"cannot prepare the insert: ${why}")
        Ok(q) ->
            bind_both(q, name, born) match
                Err(why) -> print(f"cannot bind: ${why}")
                Ok(_) ->
                    q.step() match
                        Err(why) -> print(f"cannot insert ${name}: ${why}")
                        Ok(_)    -> ()

            q.finalize()
end insert

// Both parameters, or the first failure. Each `bind_*` answers the statement back, so they chain.
bind_both(q: Query, name: string, born: int) -> Result[Query, string]
    q.bind_text(1, name) match
        Err(why) -> Err(why)
        Ok(_)    -> q.bind_int(2, born)
end bind_both

// Read the rows back. `step` answers `true` while there is a row, so the loop ends on `false` -- and
// an error ends it too, rather than being retried forever.
report(db: Db)
    db.prepare("select name, born from people order by born") match
        Err(why) -> print(f"cannot prepare the query: ${why}")
        Ok(q) ->
            var going = true

            while going
                q.step() match
                    Err(why) ->
                        print(f"the query stopped: ${why}")
                        going = false

                    Ok(false) -> going = false
                    Ok(true) ->
                        val born = q.int_at(1)

                        q.text_at(0) match
                            Ok(Some(name)) -> print(f"  ${name}%-14s ${born}")
                            Ok(None)       -> print(f"  (no name)      ${born}")
                            Err(why)       -> print(f"  unreadable: ${why}")

            q.finalize()
end report
```

The example lives here rather than in a file of its own because everything under the package root is
compiled *into* the library, and a library carries no entry point.
