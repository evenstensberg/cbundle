/*
 * lexer.h -- a minimal JavaScript scanner that extracts CommonJS
 *            `require("...")` dependencies from a source file.
 *
 * This is deliberately NOT a parser. It is a token skimmer: it knows enough
 * about JavaScript lexical grammar (comments, strings, template literals and
 * regular-expression literals) to avoid reporting a `require` that appears
 * inside one of them, and nothing more. See doc comments in lexer.c for the
 * exact set of cases handled and the known limitations.
 */
#ifndef CB_LEXER_H
#define CB_LEXER_H

#include <stddef.h>

/* A dependency found in a source file. */
typedef struct {
    char  *spec; /* the literal request string, e.g. "./math" */
    size_t line; /* 1-based line number, for diagnostics */
} Dependency;

/* A growable list of Dependency. */
typedef struct {
    Dependency *items;
    size_t      n;
    size_t      cap;
} DepList;

void deplist_init(DepList *d);
void deplist_free(DepList *d);

/* Scan `src` (of `len` bytes) and append every statically analysable
 * `require("literal")` call to `out`. Duplicate specifiers are appended only
 * once. */
void js_find_requires(const char *src, size_t len, DepList *out);

#endif /* CB_LEXER_H */
