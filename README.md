# sqlite3

A [sysl](https://github.com/sysl-lang/sysl) binding for SQLite.

The module is `sqlite`, and the directory it lives in is what names it — a module is a directory and
its name is that directory's path relative to the package root. The C that sits beside it is part of
the library: sysl compiles it as part of the build, so nothing here needs a makefile and there is no
build script anywhere in this repository.

```
sh/sysl/sqlite/
    sqlite.sysl     the binding a program calls
    c/c.sysl        the boundary as C declares it: the link, the header, the codes, the externs
    c/shim.c        the one call that needs C: `SQLITE_TRANSIENT` is a cast, not a value
package.hocon       who this package is, and what it needs of the machine
```

The module is **`sh.sysl.sqlite`**, and the three directories are that name: a dotted module name
mirrors its path from the library root. The prefix is the reverse-DNS of `sysl.sh`, so that a package
claims a name nobody else will mint rather than the top-level word `sqlite`.

**It is two layers, and the split is the same one every binding in the organisation uses.**
`sh.sysl.sqlite.c` is SQLite as C declares it and nothing else — every `extern`, the `@link`, the
`@include`, the `opaque struct` handles and the `c const` blocks that read the header's macros. The
module above it is what a program calls. A program that needs a corner of SQLite this binding has not
reached yet can import the lower one and call it directly; that is what it is for.

## Using it

Name it in your project's `package.hocon` and `sysl build` fetches it:

```hocon
dependencies {
  sqlite3 { git = "github.com/sysl-lang/sqlite3", version = "0.6.0" }
}
```

The coordinate is an identity rather than a URL, so it carries no `https://`, and `version` is the
tag `v0.6.0` here. Note that it names the **package**, `sqlite3`, while the module you import is
`sh.sysl.sqlite` — those are deliberately different things.

Or build the package into an artifact once, then compile against it:

```
sysl build-lib . -o /tmp/sqlite.syslib
sysl run yourprogram.sysl --lib /tmp/sqlite.syslib
```

**Nothing on the consuming side mentions `-lsqlite3`.** The `@link("sqlite3")` annotation lives in this
library's own header and travels inside the artifact, so a program that links is a program that did
not have to know what this binds.

## SQLite has to be installed, and nothing has to be typed

You will need SQLite's development files on the machine — `libsqlite3` and its header — since this is
a binding and not a copy. `c/c.sysl` says `@include("<sqlite3.h>")` and no copy of that header is in
this repository.

**Where they are is not your problem as of 0.6.1.** This package names SQLite in its `package.hocon`,
and the compiler asks `pkg-config` where it lives, so a build is just:

```
sysl build .
```

Until 0.6.1 that line opened with `SQLITE_H=$(xcrun --show-sdk-path)/usr/include` on macOS and pointed
at `/usr/include` on Linux, and every consuming project carried the flag.

The declaration is doing the same job it always did, and the refusal it buys is unchanged: a Linux box
with no `libsqlite3-dev` is stopped by a sentence naming SQLite rather than by the C compiler saying

```
fatal error: 'sqlite3.h' file not found
```

which names a file and knows nothing about this package or the thing to install.

`--include-path sqlite3=<dir>` still answers it and takes precedence, for a machine with no
pkg-config or a prefix you built yourself. A **bare** `--include-path` does not, deliberately — the
check asks what a build says it has rather than what it might happen to find, so that a declaration
cannot be satisfied by accident on one machine and go unenforced on the next.

**Needs sysl 0.0.56**, which is where a package gained the ability to name its library.

A program reaching this package through a **`.syslib`** needs none of this: an artifact carries the
compiled shim, so `sysl run yourprogram.sysl --lib /tmp/sqlite.syslib` wants no header and no flag.

## Nothing has to be released by hand

A connection and a prepared statement are both C's storage, and both are held behind a `&` with a
destructor. `open` answers a `&Db` that closes when the last reference to it goes, and `prepare` a
`&Query` that finalizes the same way. **There is no `close` and no `finalize` in this API** — they
were the two calls a program could forget, and now there is nothing to forget.

**The order they happen in is SQLite's rule, and the type is what keeps it.** `sqlite3_close` on a
connection that still has a live statement answers `SQLITE_BUSY` and closes nothing, so the statement
has to go first. A `Query` therefore *holds* the `Db` it was compiled by, which means the connection
cannot be destroyed while any statement from it is alive:

```sysl
// A statement handed back from the function that opened the database. The connection is still open,
// because this statement is holding it.
prepared() -> Result[&Query, Error]
    val db = open("notes.db")?

    db.prepare("select body from notes")
end prepared
```

Written by hand, that was a mistake with no symptom: the connection would be closed while the
statement was live, SQLite would refuse, and the connection would stay open for the life of the
process with nothing said. A leak rather than a crash, which is the harder kind to find.

The destructor calls `sqlite3_close` rather than `sqlite3_close_v2` for the same reason. SQLite offers
the second one for languages whose destructors run in an order nobody controls; sysl's run in the
order the references go, so the stricter call is the correct one and its refusal is unreachable.

## What it binds

Two handles, both of them C's incomplete types: `sqlite3` and `sqlite3_stmt` are declared and never
defined in SQLite's header, so no size was ever on offer. They are written in `c/c.sysl` as
`opaque struct`, which says exactly that — `*Sqlite3` and `*Stmt` get types of their own, and neither
can be crossed with the other or with the `*u8` strings declared beside them.

Almost nothing else crosses the boundary that is not a pointer or an integer: no struct to lay out,
no callback to install, no varargs. That is what makes SQLite a good first binding.

**A failure is a code as well as a sentence.** `Error` carries SQLite's own result code and the
message the connection was holding when it failed, and `kind()` turns the code into a `Code` to match
on — which is the difference between a program that retries and one that reports "database is locked"
to somebody who cannot act on it:

```sysl
db.exec(sql) match
    Ok(_)    -> ()
    Err(why) -> why.kind() match
        Busy       -> retry_later()
        Constraint -> print(f"the schema refused it: ${why}")
        _          -> print(f"${why}")
```

The codes are read from the header of the machine being built for rather than written down here, so
there is no list of numbers in this repository to drift from SQLite's. Two of the names differ from
the header's on purpose — `SQLITE_OK` is `Success` and `SQLITE_ERROR` is `Failed`, because `Ok` is
already `Result`'s and `Error` is the struct — and `Code.name` answers the header's own word for any
of them.

**How a database is opened is a `Access`, and it has three answers rather than two.** `Create` reads
and writes, making the file when it is not there; `Write` reads and writes a database that is already
there and fails with `CantOpen` when it is not; `Read` opens one read-only. The middle one is the
reason this is an enum: creating the file and opening it for writing are separate flags in SQLite's
header, and this binding or'd them together as one hard-coded number until the flags started coming
from the header. `open` and `open_readonly` are the two names for the common cases.

`NULL` is the case a binding has to have an answer for, and it has one: reading a column that holds
SQL NULL answers `Ok(None)`, which is not the same answer as a decode failure and does not
dereference the null pointer SQLite hands back.

**Text holding more than one statement needs `statements` or `exec_all`.** SQLite's `prepare`
compiles the *first* statement and hands back a pointer to what is left, so `prepare` and `exec` run
one statement and drop the rest with nothing said — which is SQLite's behaviour and not something a
binding can hide. `db.statements(sql)` is the cursor over that tail pointer, and `db.exec_all(sql)`
runs every statement in the text and answers how many ran.

```sysl
db.exec_all("create table a (x); insert into a values (1)")  // 2
db.exec("create table a (x); insert into a values (1)")      // the create, and no more
```

The cursor is stepped rather than iterated, because a `for` loop iterates a *copy* of its iterator
and the message a failed walk is carrying would then be read off a cursor that never moved. Ask
`walk.error()` after the loop.

## Example

```sysl
import sh.sysl.sqlite.*

// An in-memory database, so the program leaves nothing behind and needs no writable directory. The
// name is SQLite's own: it is not a path.
//
// Every failure here is a `sqlite.Error`, so `?` carries each one out to `main`, which prints it.
// Nothing closes the connection and nothing finalizes a statement -- both go when the last reference
// to them does, and the connection goes after the statements it compiled.
main() -> Result[unit, Error]
    val db = open(":memory:")?

    db.exec("create table people (name text, born integer)")?

    // One prepared statement, run three times. `bind_*` numbers parameters from 1, which is SQLite's
    // numbering rather than this binding's, and each answers the statement back so they chain.
    val add = db.prepare("insert into people (name, born) values (?, ?)")?

    add.bind_text(1, "Ada Lovelace")?.bind_int(2, 1815)?.step()?
    add.reset()?.bind_text(1, "Alan Turing")?.bind_int(2, 1912)?.step()?
    add.reset()?.bind_text(1, "Grace Hopper")?.bind_int(2, 1906)?.step()?

    // `step` answers `true` while there is a row, so the loop ends on `false` -- and a failure leaves
    // it through `?` rather than being retried forever.
    val q = db.prepare("select name, born from people order by born")?

    while q.step()?
        val born = q.int_at(1)

        q.text_at(0)? match
            Some(name) -> print(f"  ${name}%-14s ${born}")
            None       -> print(f"  (no name)      ${born}")

    Ok(())
end main
```

```
  Ada Lovelace   1815
  Grace Hopper   1906
  Alan Turing    1912
```

The example lives here rather than in a file of its own because everything under the package root is
compiled *into* the library, and a library carries no entry point.

## The tests

They run against a real SQLite, so they need one installed — but they no longer take a flag for it,
which used to be the one thing about this package that differed from its siblings:

```
sysl test .
```

Nearly all of them open `:memory:`, which SQLite treats as a private database living for as long as the
connection, so there is no file to clean up and no order dependence between them. The exception is the
one whose subject is what happens when there is *no* file, and it removes the file first rather than
trusting the last run to have finished tidily.
