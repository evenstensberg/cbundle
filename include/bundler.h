/*
 * bundler.h -- the module graph and the code generator.
 *
 * A Graph holds every module reachable from the entry point. Modules are
 * identified by a small integer id; id 0 is always the entry module. Building
 * the graph is a depth-first walk: read a file, scan it for require() calls,
 * resolve each one, and recurse into anything not seen before. Cycles are
 * handled naturally because a module is registered before its dependencies
 * are visited.
 */
#ifndef CB_BUNDLER_H
#define CB_BUNDLER_H

#include "util.h"

/* One resolved edge of the graph: the literal request string as written in the
 * source, plus the id of the module it resolves to. */
typedef struct {
    char *spec;
    int   target;
} Edge;

typedef struct {
    char  *path;    /* absolute, normalized path on disk */
    char  *dir;     /* dirname(path), cached for resolution */
    char  *name;    /* short display path, relative to the project root */
    char  *source;  /* file contents, NUL-terminated */
    size_t len;
    int    id;
    int    is_json; /* JSON modules are wrapped instead of executed */
    Edge  *edges;
    size_t nedges;
} Module;

typedef struct {
    Module **mods;
    size_t   n, cap;
    char    *root;    /* directory used to shorten displayed paths */
    int      verbose; /* log each resolved module to stderr */
} Graph;

/* Options controlling code generation. */
typedef struct {
    const char *global_name; /* if non-NULL, also expose exports as this global */
    const char *entry_name;  /* display name of the entry, used in the banner */
    int         no_banner;   /* suppress the generated-by header comment */
} EmitOptions;

Graph *graph_new(const char *root, int verbose);
void   graph_free(Graph *g);

/* Populate `g` starting from `entry_path` (an existing file path).
 * Terminates the process with a diagnostic if a dependency cannot be resolved
 * or a file cannot be read. */
void graph_build(Graph *g, const char *entry_path);

/* Append the finished bundle to `out`. */
void graph_emit(const Graph *g, Buffer *out, const EmitOptions *opt);

/* Print the dependency graph in a human-readable form (for --graph). */
void graph_print(const Graph *g, FILE *f);

#endif /* CB_BUNDLER_H */
