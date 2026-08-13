/* The one thing a SQLite binding still cannot say in sysl.
 *
 * This file used to hold five more functions, each returning a `#define` --
 * `SQLITE_OK`, `SQLITE_ROW` and their siblings -- because a macro has no symbol
 * for an `extern` to name. They are gone: `c/c.sysl` now asks the C compiler
 * for those values directly, in a `c const` block, and gets constants rather
 * than calls. That is the better answer as well as the shorter one, since a
 * constant may stand in a `match` arm and be folded into a comparison.
 *
 * What is left is not a value at all. `SQLITE_TRANSIENT` is the cast
 * `((sqlite3_destructor_type)-1)` -- a function pointer whose value no signature
 * describes, and which nothing outside C can construct. `sqlite3_bind_text`
 * cannot be called correctly without it, so the call is made here and the sysl
 * side never sees it.
 *
 * Nothing in this file is sysl-specific. It is the ordinary shim any language
 * writes when it binds a C library, and `build-lib` compiles it because it is
 * sitting in the library's tree.
 */
#include <sqlite3.h>

/* Bind a NUL-terminated string, telling SQLite to take its own copy.
 *
 * The copy is not an optimisation to skip: without it SQLite keeps the caller's
 * pointer until the statement is reset, and the sysl string that pointer came
 * from may be gone by then. `SQLITE_TRANSIENT` is what asks for the copy, and
 * passing it is the whole reason this wrapper exists.
 */
int sysl_sqlite_bind_text(sqlite3_stmt *stmt, int index, const char *value) {
    return sqlite3_bind_text(stmt, index, value, -1, SQLITE_TRANSIENT);
}
