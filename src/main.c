/*
 * main.c -- command line front end for cbundle.
 *
 * cbundle [options] <entry.js>
 *
 * Reads an entry file, follows its CommonJS require() graph and writes a
 * single self-contained JavaScript file to stdout or to -o.
 */
#include "bundler.h"
#include "path.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>

#ifndef CBUNDLE_VERSION
#define CBUNDLE_VERSION "0.1.0"
#endif

static const char *PROGRAM = "cbundle";

static void usage(FILE *f)
{
    fprintf(f,
"Usage: %s [options] <entry.js>\n"
"\n"
"Bundle a CommonJS module graph into a single JavaScript file.\n"
"\n"
"Options:\n"
"  -o, --output FILE    write the bundle to FILE (default: stdout)\n"
"  -g, --global NAME    also assign the entry module's exports to a global\n"
"  -r, --root DIR       project root used to shorten paths in the output\n"
"                       (default: the entry file's directory)\n"
"      --graph          print the dependency graph instead of a bundle\n"
"      --no-banner      omit the generated-by header comment\n"
"  -v, --verbose        list each module as it is resolved, on stderr\n"
"  -h, --help           show this message and exit\n"
"  -V, --version        show the version and exit\n"
"\n"
"Examples:\n"
"  %s src/index.js -o dist/bundle.js\n"
"  %s src/index.js --global MyLib -o dist/mylib.js\n"
"  %s src/index.js --graph\n",
            PROGRAM, PROGRAM, PROGRAM, PROGRAM);
}

/* Return the value for an option that requires an argument, or die. */
static const char *needs_value(int argc, char **argv, int *i)
{
    if (*i + 1 >= argc)
        cb_die("option '%s' requires an argument", argv[*i]);
    return argv[++(*i)];
}

int main(int argc, char **argv)
{
    const char *entry = NULL;
    const char *output = NULL;
    const char *global_name = NULL;
    const char *root_opt = NULL;
    int show_graph = 0, no_banner = 0, verbose = 0;
    int i, only_files = 0;

    char *root;
    Graph *g;
    Buffer out;
    EmitOptions opt;
    char *entry_name;

    for (i = 1; i < argc; i++) {
        const char *a = argv[i];

        if (!only_files && a[0] == '-' && a[1] != '\0') {
            if (!strcmp(a, "--")) {
                only_files = 1;
            } else if (!strcmp(a, "-o") || !strcmp(a, "--output")) {
                output = needs_value(argc, argv, &i);
            } else if (!strcmp(a, "-g") || !strcmp(a, "--global")) {
                global_name = needs_value(argc, argv, &i);
            } else if (!strcmp(a, "-r") || !strcmp(a, "--root")) {
                root_opt = needs_value(argc, argv, &i);
            } else if (!strcmp(a, "--graph")) {
                show_graph = 1;
            } else if (!strcmp(a, "--no-banner")) {
                no_banner = 1;
            } else if (!strcmp(a, "-v") || !strcmp(a, "--verbose")) {
                verbose = 1;
            } else if (!strcmp(a, "-h") || !strcmp(a, "--help")) {
                usage(stdout);
                return EXIT_SUCCESS;
            } else if (!strcmp(a, "-V") || !strcmp(a, "--version")) {
                printf("%s %s\n", PROGRAM, CBUNDLE_VERSION);
                return EXIT_SUCCESS;
            } else {
                fprintf(stderr, "%s: unknown option '%s'\n", PROGRAM, a);
                usage(stderr);
                return EXIT_FAILURE;
            }
            continue;
        }

        if (entry)
            cb_die("only one entry file may be given (got '%s' and '%s')",
                   entry, a);
        entry = a;
    }

    if (!entry) {
        usage(stderr);
        return EXIT_FAILURE;
    }

    /* Paths inside the bundle are printed relative to the root, which keeps
     * the output stable across machines and checkouts. */
    if (root_opt) {
        root = path_absolute(root_opt);
    } else {
        char *abs = path_absolute(entry);
        root = path_dirname(abs);
        free(abs);
    }

    g = graph_new(root, verbose);
    graph_build(g, entry);

    if (show_graph) {
        graph_print(g, stdout);
        graph_free(g);
        free(root);
        return EXIT_SUCCESS;
    }

    entry_name = cb_strdup(g->mods[0]->name);

    opt.global_name = global_name;
    opt.entry_name = entry_name;
    opt.no_banner = no_banner;

    buf_init(&out);
    graph_emit(g, &out, &opt);

    if (output) {
        if (cb_write_file(output, out.data, out.len) != 0)
            cb_die("cannot write '%s'", output);
        if (verbose)
            fprintf(stderr, "cbundle: wrote %s (%zu bytes, %zu modules)\n",
                    output, out.len, g->n);
    } else {
        fwrite(out.data, 1, out.len, stdout);
    }

    buf_free(&out);
    free(entry_name);
    graph_free(g);
    free(root);
    return EXIT_SUCCESS;
}
