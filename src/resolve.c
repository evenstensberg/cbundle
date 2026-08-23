#include "resolve.h"
#include "path.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

const char *const RESOLVE_EXTENSIONS[] = { ".js", ".cjs", ".json", NULL };

/* Return `cand` (as a normalized absolute path) if it is a regular file. */
static char *accept(char *cand)
{
    char *norm;

    if (!path_is_file(cand)) {
        free(cand);
        return NULL;
    }
    norm = path_normalize(cand);
    free(cand);
    return norm;
}

/* LOAD_AS_FILE(X): X, then X + each extension. */
static char *load_as_file(const char *x)
{
    char *hit;
    size_t i;

    if ((hit = accept(cb_strdup(x))))
        return hit;

    for (i = 0; RESOLVE_EXTENSIONS[i]; i++) {
        Buffer b;
        buf_init(&b);
        buf_puts(&b, x);
        buf_puts(&b, RESOLVE_EXTENSIONS[i]);
        if ((hit = accept(b.data)))
            return hit;
    }
    return NULL;
}

/* LOAD_INDEX(X): X/index + each extension. */
static char *load_index(const char *x)
{
    char *idx = path_join(x, "index");
    char *hit = NULL;
    size_t i;

    for (i = 0; RESOLVE_EXTENSIONS[i]; i++) {
        Buffer b;
        buf_init(&b);
        buf_puts(&b, idx);
        buf_puts(&b, RESOLVE_EXTENSIONS[i]);
        if ((hit = accept(b.data)))
            break;
    }
    free(idx);
    return hit;
}

/*
 * Extract a top-level string field from JSON text.
 *
 * A full JSON parser is overkill here: we only need package.json's "main".
 * The scan skips nested objects/arrays so that a "main" key inside, say,
 * "scripts" is not mistaken for the real one, and it understands string
 * escaping well enough not to be fooled by braces inside strings.
 *
 * Returns a malloc'd value, or NULL when the field is absent.
 */
static char *json_top_level_string(const char *json, const char *field)
{
    size_t depth = 0;
    const char *p = json;
    size_t flen = strlen(field);

    while (*p) {
        if (*p == '{' || *p == '[') {
            depth++;
            p++;
            continue;
        }
        if (*p == '}' || *p == ']') {
            if (depth)
                depth--;
            p++;
            continue;
        }
        if (*p == '"') {
            const char *start = ++p;
            while (*p && *p != '"') {
                if (*p == '\\' && p[1])
                    p++;
                p++;
            }
            if (!*p)
                return NULL;
            /* A key is a string at depth 1 followed by ':'. */
            if (depth == 1 && (size_t)(p - start) == flen &&
                strncmp(start, field, flen) == 0) {
                const char *q = p + 1;
                while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                    q++;
                if (*q == ':') {
                    q++;
                    while (*q == ' ' || *q == '\t' || *q == '\n' || *q == '\r')
                        q++;
                    if (*q == '"') {
                        const char *vs = ++q;
                        while (*q && *q != '"') {
                            if (*q == '\\' && q[1])
                                q++;
                            q++;
                        }
                        return cb_strndup(vs, (size_t)(q - vs));
                    }
                    return NULL; /* present but not a string */
                }
            }
            p++;
            continue;
        }
        p++;
    }
    return NULL;
}

/* LOAD_AS_DIRECTORY(X): package.json "main", else X/index.<ext>. */
static char *load_as_directory(const char *x)
{
    char *pkg = path_join(x, "package.json");
    char *hit = NULL;

    if (path_is_file(pkg)) {
        size_t len;
        char *text = cb_read_file(pkg, &len);
        if (text) {
            char *main_field = json_top_level_string(text, "main");
            if (main_field && *main_field) {
                char *cand = path_join(x, main_field);
                hit = load_as_file(cand);
                if (!hit)
                    hit = load_index(cand);
                free(cand);
            }
            free(main_field);
            free(text);
        }
    }
    free(pkg);

    if (!hit)
        hit = load_index(x);
    return hit;
}

/* NODE_MODULES_PATHS(dir): walk up, trying <dir>/node_modules/<spec>. */
static char *load_node_module(const char *spec, const char *importer_dir)
{
    char *dir = path_absolute(importer_dir);

    for (;;) {
        char *parent;

        /* Skip directories that are themselves named node_modules, matching
         * Node's behaviour of not nesting node_modules/node_modules. */
        size_t dlen = strlen(dir);
        if (!(dlen >= 13 && strcmp(dir + dlen - 13, "/node_modules") == 0)) {
            char *nm = path_join(dir, "node_modules");
            char *cand = path_join(nm, spec);
            char *hit = load_as_file(cand);
            if (!hit)
                hit = load_as_directory(cand);
            free(cand);
            free(nm);
            if (hit) {
                free(dir);
                return hit;
            }
        }

        if (strcmp(dir, "/") == 0)
            break;
        parent = path_dirname(dir);
        free(dir);
        dir = parent;
    }
    free(dir);
    return NULL;
}

char *resolve_specifier(const char *spec, const char *importer_dir)
{
    char *hit;

    if (!*spec)
        return NULL;

    if (path_is_relative_spec(spec)) {
        char *base = path_join(importer_dir, spec);
        char *cand = path_absolute(base);
        free(base);

        hit = load_as_file(cand);
        if (!hit)
            hit = load_as_directory(cand);
        free(cand);
        return hit;
    }

    return load_node_module(spec, importer_dir);
}
