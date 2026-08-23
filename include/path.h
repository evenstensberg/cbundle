/*
 * path.h -- POSIX path manipulation used by module resolution.
 *
 * All functions that return `char *` return a freshly malloc'd string that the
 * caller owns. Paths are treated as '/'-separated byte strings.
 */
#ifndef CB_PATH_H
#define CB_PATH_H

/* "a/b/c.js" -> "a/b";  "c.js" -> ".";  "/x" -> "/" */
char *path_dirname(const char *p);

/* Join two components with a single '/'. If `b` is absolute, `b` wins. */
char *path_join(const char *a, const char *b);

/* Collapse ".", "..", duplicate and trailing slashes. Purely lexical: the
 * filesystem is never consulted, so symlinks are not followed. */
char *path_normalize(const char *p);

/* Make `p` absolute against the current working directory, then normalize. */
char *path_absolute(const char *p);

/* Express `p` relative to `base` when possible; otherwise return `p` as-is.
 * Used only to print short, readable paths in the bundle and in diagnostics. */
char *path_relative(const char *base, const char *p);

/* True for specifiers Node treats as paths: "./x", "../x" and "/x". */
int path_is_relative_spec(const char *spec);

/* Filesystem probes. */
int path_is_file(const char *p);
int path_is_dir(const char *p);

/* Pointer to the extension including the dot ("a/b.js" -> ".js"), or "" when
 * there is none. Points into `p`; do not free. */
const char *path_ext(const char *p);

#endif /* CB_PATH_H */
