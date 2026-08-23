/*
 * lexer.c -- see lexer.h.
 *
 * What is handled
 * ---------------
 *   - line comments, block comments
 *   - single- and double-quoted strings, with escapes and line continuations
 *   - template literals, including nested `${ ... }` substitutions (so a
 *     require() inside a substitution is still found)
 *   - regular-expression literals, disambiguated from division using the
 *     previous significant token
 *   - `require` as a standalone identifier only: `foo.require(x)` and
 *     `myrequire(x)` are ignored
 *
 * Known limitations (documented, not accidental)
 * ----------------------------------------------
 *   - only string-literal arguments are collected; `require(name)` with a
 *     computed argument cannot be resolved statically and is skipped
 *   - `require` may be shadowed by a local binding; that is not tracked
 *   - ESM `import` syntax is not supported
 */
#include "lexer.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

void deplist_init(DepList *d)
{
    d->items = NULL;
    d->n = d->cap = 0;
}

void deplist_free(DepList *d)
{
    size_t i;
    for (i = 0; i < d->n; i++)
        free(d->items[i].spec);
    free(d->items);
    deplist_init(d);
}

static void deplist_push(DepList *d, const char *spec, size_t len, size_t line)
{
    size_t i;

    for (i = 0; i < d->n; i++) /* de-duplicate: one edge per specifier */
        if (strlen(d->items[i].spec) == len &&
            strncmp(d->items[i].spec, spec, len) == 0)
            return;

    if (d->n == d->cap) {
        d->cap = d->cap ? d->cap * 2 : 8;
        d->items = cb_realloc(d->items, d->cap * sizeof *d->items);
    }
    d->items[d->n].spec = cb_strndup(spec, len);
    d->items[d->n].line = line;
    d->n++;
}

/* --------------------------------------------------------------- scanner */

/* The previous significant token, which decides whether a '/' starts a regular
 * expression or is a division operator. */
typedef enum {
    PREV_NONE,     /* start of input, or after a token that allows a regex */
    PREV_OPERAND   /* identifier, literal, ')', ']', '}' -- '/' means divide */
} PrevToken;

typedef struct {
    const char *src;
    size_t      len;
    size_t      i;
    size_t      line;
    PrevToken   prev;
    /* Stack of brace depths at which an open `${` substitution began. When the
     * brace depth falls back to a recorded value, we are leaving the
     * substitution and re-entering the template literal body. */
    size_t     *tmpl;
    size_t      tmpl_n, tmpl_cap;
    size_t      depth; /* current '{' nesting depth */
} Scanner;

static int is_ident_start(int c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' ||
           c == '$' || (unsigned char)c >= 0x80; /* be generous with unicode */
}

static int is_ident_part(int c)
{
    return is_ident_start(c) || (c >= '0' && c <= '9');
}

/* Keywords after which a '/' begins a regular expression rather than a
 * division, e.g. `return /re/.test(s)`. */
static int keyword_allows_regex(const char *s, size_t n)
{
    static const char *kw[] = {
        "return", "typeof", "instanceof", "in", "of", "new", "delete", "void",
        "throw", "case", "do", "else", "yield", "await", "(", NULL
    };
    size_t k;
    for (k = 0; kw[k]; k++)
        if (strlen(kw[k]) == n && strncmp(kw[k], s, n) == 0)
            return 1;
    return 0;
}

static void tmpl_push(Scanner *sc, size_t depth)
{
    if (sc->tmpl_n == sc->tmpl_cap) {
        sc->tmpl_cap = sc->tmpl_cap ? sc->tmpl_cap * 2 : 4;
        sc->tmpl = cb_realloc(sc->tmpl, sc->tmpl_cap * sizeof *sc->tmpl);
    }
    sc->tmpl[sc->tmpl_n++] = depth;
}

/* Advance one byte, tracking line numbers. */
static void advance(Scanner *sc)
{
    if (sc->src[sc->i] == '\n')
        sc->line++;
    sc->i++;
}

static void skip_string(Scanner *sc, char quote)
{
    advance(sc); /* opening quote */
    while (sc->i < sc->len) {
        char c = sc->src[sc->i];
        if (c == '\\') {
            advance(sc);
            if (sc->i < sc->len)
                advance(sc);
            continue;
        }
        advance(sc);
        if (c == quote)
            return;
        if (c == '\n') /* unterminated string; resync at the newline */
            return;
    }
}

/* Scan a template literal body starting just after a '`' or after leaving a
 * '${...}' substitution. Returns when the closing '`' is consumed, or pushes a
 * substitution context and returns after consuming "${". */
static void scan_template_body(Scanner *sc)
{
    while (sc->i < sc->len) {
        char c = sc->src[sc->i];
        if (c == '\\') {
            advance(sc);
            if (sc->i < sc->len)
                advance(sc);
            continue;
        }
        if (c == '`') {
            advance(sc);
            sc->prev = PREV_OPERAND;
            return;
        }
        if (c == '$' && sc->i + 1 < sc->len && sc->src[sc->i + 1] == '{') {
            advance(sc); /* $ */
            advance(sc); /* { */
            tmpl_push(sc, sc->depth);
            sc->depth++;
            sc->prev = PREV_NONE;
            return; /* the main loop now scans the substitution expression */
        }
        advance(sc);
    }
}

static void skip_regex(Scanner *sc)
{
    int in_class = 0;

    advance(sc); /* opening '/' */
    while (sc->i < sc->len) {
        char c = sc->src[sc->i];
        if (c == '\\') {
            advance(sc);
            if (sc->i < sc->len)
                advance(sc);
            continue;
        }
        if (c == '\n')
            return; /* unterminated: bail out rather than swallow the file */
        advance(sc);
        if (c == '[')
            in_class = 1;
        else if (c == ']')
            in_class = 0;
        else if (c == '/' && !in_class)
            break;
    }
    while (sc->i < sc->len && is_ident_part(sc->src[sc->i])) /* flags */
        advance(sc);
}

/* Skip whitespace and comments; used between `require`, '(' and the argument. */
static void skip_trivia(Scanner *sc)
{
    while (sc->i < sc->len) {
        char c = sc->src[sc->i];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' ||
            c == '\v') {
            advance(sc);
        } else if (c == '/' && sc->i + 1 < sc->len && sc->src[sc->i + 1] == '/') {
            while (sc->i < sc->len && sc->src[sc->i] != '\n')
                advance(sc);
        } else if (c == '/' && sc->i + 1 < sc->len && sc->src[sc->i + 1] == '*') {
            advance(sc);
            advance(sc);
            while (sc->i + 1 < sc->len &&
                   !(sc->src[sc->i] == '*' && sc->src[sc->i + 1] == '/'))
                advance(sc);
            if (sc->i + 1 < sc->len) {
                advance(sc);
                advance(sc);
            } else {
                sc->i = sc->len;
            }
        } else {
            return;
        }
    }
}

/*
 * We are positioned right after the identifier `require`. Try to match
 * `( "literal" )` and, on success, record the dependency and leave `i` after
 * the closing quote. On failure nothing is consumed.
 */
static void try_match_require(Scanner *sc, DepList *out)
{
    size_t save = sc->i, save_line = sc->line;
    char quote;
    size_t start;

    skip_trivia(sc);
    if (sc->i >= sc->len || sc->src[sc->i] != '(')
        goto rollback;
    advance(sc);

    skip_trivia(sc);
    if (sc->i >= sc->len)
        goto rollback;
    quote = sc->src[sc->i];
    if (quote != '"' && quote != '\'' && quote != '`')
        goto rollback; /* computed request: not statically analysable */

    advance(sc);
    start = sc->i;
    while (sc->i < sc->len && sc->src[sc->i] != quote) {
        if (sc->src[sc->i] == '\\' || sc->src[sc->i] == '\n')
            goto rollback; /* escapes/newlines in a specifier: give up */
        if (quote == '`' && sc->src[sc->i] == '$')
            goto rollback; /* interpolated template: not a literal */
        advance(sc);
    }
    if (sc->i >= sc->len)
        goto rollback;

    deplist_push(out, sc->src + start, sc->i - start, save_line);
    advance(sc); /* closing quote */
    sc->prev = PREV_OPERAND;
    return;

rollback:
    sc->i = save;
    sc->line = save_line;
}

void js_find_requires(const char *src, size_t len, DepList *out)
{
    Scanner sc;

    sc.src = src;
    sc.len = len;
    sc.i = 0;
    sc.line = 1;
    sc.prev = PREV_NONE;
    sc.tmpl = NULL;
    sc.tmpl_n = sc.tmpl_cap = 0;
    sc.depth = 0;

    while (sc.i < len) {
        char c = src[sc.i];

        /* --- comments and regex/division ------------------------------- */
        if (c == '/' && sc.i + 1 < len) {
            char d = src[sc.i + 1];
            if (d == '/') {
                while (sc.i < len && src[sc.i] != '\n')
                    advance(&sc);
                continue;
            }
            if (d == '*') {
                advance(&sc);
                advance(&sc);
                while (sc.i + 1 < len &&
                       !(src[sc.i] == '*' && src[sc.i + 1] == '/'))
                    advance(&sc);
                if (sc.i + 1 < len) {
                    advance(&sc);
                    advance(&sc);
                } else {
                    sc.i = len;
                }
                continue;
            }
            if (sc.prev == PREV_NONE) {
                skip_regex(&sc);
                sc.prev = PREV_OPERAND;
                continue;
            }
            advance(&sc); /* division */
            sc.prev = PREV_NONE;
            continue;
        }

        /* --- literals --------------------------------------------------- */
        if (c == '"' || c == '\'') {
            skip_string(&sc, c);
            sc.prev = PREV_OPERAND;
            continue;
        }
        if (c == '`') {
            advance(&sc);
            scan_template_body(&sc);
            continue;
        }

        /* --- braces, including template substitution boundaries --------- */
        if (c == '{') {
            advance(&sc);
            sc.depth++;
            sc.prev = PREV_NONE;
            continue;
        }
        if (c == '}') {
            if (sc.tmpl_n > 0 && sc.depth - 1 == sc.tmpl[sc.tmpl_n - 1]) {
                /* end of a `${ ... }`: resume the enclosing template body */
                sc.tmpl_n--;
                sc.depth--;
                advance(&sc);
                scan_template_body(&sc);
                continue;
            }
            advance(&sc);
            if (sc.depth > 0)
                sc.depth--;
            sc.prev = PREV_OPERAND;
            continue;
        }

        /* --- identifiers and keywords ----------------------------------- */
        if (is_ident_start((unsigned char)c)) {
            size_t start = sc.i;
            size_t n;
            while (sc.i < len && is_ident_part((unsigned char)src[sc.i]))
                advance(&sc);
            n = sc.i - start;

            /* A member access such as `obj.require(...)` is not our require. */
            {
                size_t k = start;
                int is_member = 0;
                while (k > 0) {
                    char p = src[k - 1];
                    if (p == ' ' || p == '\t' || p == '\n' || p == '\r') {
                        k--;
                        continue;
                    }
                    is_member = (p == '.');
                    break;
                }
                if (!is_member && n == 7 && strncmp(src + start, "require", 7) == 0)
                    try_match_require(&sc, out);
            }

            sc.prev = keyword_allows_regex(src + start, n) ? PREV_NONE
                                                           : PREV_OPERAND;
            continue;
        }

        /* --- numbers ---------------------------------------------------- */
        if (c >= '0' && c <= '9') {
            while (sc.i < len &&
                   (is_ident_part((unsigned char)src[sc.i]) || src[sc.i] == '.'))
                advance(&sc);
            sc.prev = PREV_OPERAND;
            continue;
        }

        /* --- punctuation ------------------------------------------------ */
        advance(&sc);
        sc.prev = (c == ')' || c == ']') ? PREV_OPERAND : PREV_NONE;
    }

    free(sc.tmpl);
}
