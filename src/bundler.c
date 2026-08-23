#include "bundler.h"
#include "lexer.h"
#include "path.h"
#include "resolve.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

/* ----------------------------------------------------------------- graph */

Graph *graph_new(const char *root, int verbose)
{
    Graph *g = cb_malloc(sizeof *g);
    g->mods = NULL;
    g->n = g->cap = 0;
    g->root = cb_strdup(root);
    g->verbose = verbose;
    return g;
}

void graph_free(Graph *g)
{
    size_t i, j;

    if (!g)
        return;
    for (i = 0; i < g->n; i++) {
        Module *m = g->mods[i];
        for (j = 0; j < m->nedges; j++)
            free(m->edges[j].spec);
        free(m->edges);
        free(m->path);
        free(m->dir);
        free(m->name);
        free(m->source);
        free(m);
    }
    free(g->mods);
    free(g->root);
    free(g);
}

/* Linear lookup by path. Bundles are small (tens to hundreds of modules), so
 * the O(n) scan is not worth replacing with a hash table here. */
static Module *graph_find(const Graph *g, const char *path)
{
    size_t i;
    for (i = 0; i < g->n; i++)
        if (strcmp(g->mods[i]->path, path) == 0)
            return g->mods[i];
    return NULL;
}

static Module *graph_add(Graph *g, const char *path)
{
    Module *m = cb_malloc(sizeof *m);

    m->path = cb_strdup(path);
    m->dir = path_dirname(path);
    m->name = path_relative(g->root, path);
    m->source = NULL;
    m->len = 0;
    m->id = (int)g->n;
    m->is_json = strcmp(path_ext(path), ".json") == 0;
    m->edges = NULL;
    m->nedges = 0;

    if (g->n == g->cap) {
        g->cap = g->cap ? g->cap * 2 : 16;
        g->mods = cb_realloc(g->mods, g->cap * sizeof *g->mods);
    }
    g->mods[g->n++] = m;

    if (g->verbose)
        fprintf(stderr, "cbundle: [%d] %s\n", m->id, m->name);
    return m;
}

/*
 * Load a module's source and recurse into its dependencies.
 *
 * The module is added to the graph *before* its dependencies are visited, so a
 * cycle (a -> b -> a) simply finds `a` already present and stops.
 */
static Module *graph_visit(Graph *g, const char *path)
{
    Module *m = graph_find(g, path);
    DepList deps;
    size_t i;

    if (m)
        return m;

    m = graph_add(g, path);
    m->source = cb_read_file(path, &m->len);
    if (!m->source)
        cb_die("cannot read '%s'", path);

    /* A leading "#!/usr/bin/env node" is not valid inside a function body.
     * Rewriting the '#!' to '//' neutralises it without shifting any byte
     * offsets, so reported line numbers stay accurate. */
    if (m->len >= 2 && m->source[0] == '#' && m->source[1] == '!') {
        m->source[0] = '/';
        m->source[1] = '/';
    }

    if (m->is_json)
        return m; /* JSON has no dependencies */

    deplist_init(&deps);
    js_find_requires(m->source, m->len, &deps);

    m->nedges = deps.n;
    m->edges = deps.n ? cb_malloc(deps.n * sizeof *m->edges) : NULL;

    for (i = 0; i < deps.n; i++) {
        char *resolved = resolve_specifier(deps.items[i].spec, m->dir);
        Module *dep;

        if (!resolved)
            cb_die("cannot resolve '%s' from %s:%zu", deps.items[i].spec,
                   m->name, deps.items[i].line);

        dep = graph_visit(g, resolved);
        free(resolved);

        /* graph_visit may have reallocated g->mods, but Module pointers are
         * stable because each Module is heap-allocated on its own. */
        m->edges[i].spec = cb_strdup(deps.items[i].spec);
        m->edges[i].target = dep->id;
    }

    deplist_free(&deps);
    return m;
}

void graph_build(Graph *g, const char *entry_path)
{
    char *abs = path_absolute(entry_path);

    if (!path_is_file(abs))
        cb_die("entry file '%s' does not exist", entry_path);
    graph_visit(g, abs);
    free(abs);
}

void graph_print(const Graph *g, FILE *f)
{
    size_t i, j;

    for (i = 0; i < g->n; i++) {
        const Module *m = g->mods[i];
        fprintf(f, "[%d] %s\n", m->id, m->name);
        for (j = 0; j < m->nedges; j++)
            fprintf(f, "      %-28s -> [%d] %s\n", m->edges[j].spec,
                    m->edges[j].target, g->mods[m->edges[j].target]->name);
    }
    fprintf(f, "\n%zu module%s\n", g->n, g->n == 1 ? "" : "s");
}

/* ------------------------------------------------------------- code gen */

/*
 * The runtime prelude. It is a tiny CommonJS loader:
 *
 *   - `modules` maps a numeric id to { file, dir, deps, fn }
 *   - `deps` maps the literal request string to the id it resolved to at
 *     build time, so module source never has to be rewritten
 *   - `cache` is populated before `fn` runs, which is what makes circular
 *     requires behave the same way they do in Node (the partially filled
 *     exports object is returned)
 */
static const char RUNTIME[] =
"(function (modules, entryId) {\n"
"  \"use strict\";\n"
"  var cache = {};\n"
"\n"
"  function cbundleRequire(id) {\n"
"    var cached = cache[id];\n"
"    if (cached !== undefined) return cached.exports;\n"
"\n"
"    var def = modules[id];\n"
"    if (def === undefined) {\n"
"      throw new Error(\"cbundle: module \" + id + \" is missing from the bundle\");\n"
"    }\n"
"\n"
"    // Register before executing so that a cycle sees a partial exports\n"
"    // object instead of recursing forever -- same semantics as Node.\n"
"    var module = (cache[id] = { id: id, exports: {}, loaded: false });\n"
"\n"
"    function localRequire(request) {\n"
"      return cbundleRequire(resolveId(request));\n"
"    }\n"
"    function resolveId(request) {\n"
"      var target = def.deps[request];\n"
"      if (target === undefined) {\n"
"        throw new Error(\n"
"          \"cbundle: '\" + request + \"' was required by \" + def.file +\n"
"          \" but is not in the bundle (was it a computed require?)\"\n"
"        );\n"
"      }\n"
"      return target;\n"
"    }\n"
"    localRequire.resolve = function (request) {\n"
"      return modules[resolveId(request)].file;\n"
"    };\n"
"    localRequire.cache = cache;\n"
"\n"
"    def.fn.call(module.exports, module, module.exports, localRequire,\n"
"                def.file, def.dir);\n"
"    module.loaded = true;\n"
"    return module.exports;\n"
"  }\n"
"\n"
"  return cbundleRequire(entryId);\n"
"})";

/* Escape any "*" followed by "/" so module paths cannot close the comment
 * we are writing them into. */
static void put_comment_text(Buffer *out, const char *s)
{
    for (; *s; s++) {
        if (s[0] == '*' && s[1] == '/') {
            buf_puts(out, "*\\/");
            s++;
        } else {
            buf_putc(out, *s);
        }
    }
}

void graph_emit(const Graph *g, Buffer *out, const EmitOptions *opt)
{
    size_t i, j;

    if (!opt->no_banner) {
        buf_puts(out, "/* Bundled by cbundle. Entry: ");
        put_comment_text(out, opt->entry_name ? opt->entry_name : "?");
        buf_printf(out, " (%zu module%s). Do not edit. */\n", g->n,
                   g->n == 1 ? "" : "s");
    }

    if (opt->global_name)
        buf_printf(out, "var %s = ", opt->global_name);

    buf_puts(out, RUNTIME);
    buf_puts(out, "({\n");

    for (i = 0; i < g->n; i++) {
        const Module *m = g->mods[i];

        buf_printf(out, "  %d: {\n", m->id);

        buf_puts(out, "    file: ");
        buf_put_json_string(out, m->name);
        buf_puts(out, ",\n    dir: ");
        {
            char *rel = path_relative(g->root, m->dir);
            buf_put_json_string(out, rel);
            free(rel);
        }
        buf_puts(out, ",\n    deps: {");
        for (j = 0; j < m->nedges; j++) {
            if (j)
                buf_putc(out, ',');
            buf_putc(out, ' ');
            buf_put_json_string(out, m->edges[j].spec);
            buf_printf(out, ": %d", m->edges[j].target);
        }
        buf_puts(out, m->nedges ? " },\n" : "},\n");

        buf_puts(out, "    fn: function (module, exports, require, "
                      "__filename, __dirname) {\n");
        buf_puts(out, "/*--- ");
        put_comment_text(out, m->name);
        buf_puts(out, " ---*/\n");

        if (m->is_json) {
            /* JSON is data, not code: hand it straight to module.exports. */
            buf_puts(out, "module.exports = ");
            buf_put(out, m->source, m->len);
            buf_puts(out, ";\n");
        } else {
            buf_put(out, m->source, m->len);
            /* Guarantee a newline so a trailing // comment in the module
             * cannot swallow the closing brace below. */
            if (m->len == 0 || m->source[m->len - 1] != '\n')
                buf_putc(out, '\n');
        }

        buf_puts(out, "    }\n");
        buf_printf(out, "  }%s\n", i + 1 < g->n ? "," : "");
    }

    buf_puts(out, "}, 0);\n");

    if (opt->global_name)
        buf_printf(out,
                   "if (typeof globalThis !== \"undefined\") "
                   "globalThis.%s = %s;\n",
                   opt->global_name, opt->global_name);
}
