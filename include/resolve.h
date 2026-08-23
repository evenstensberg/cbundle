/*
 * resolve.h -- Node-style module resolution.
 *
 * Implements the practical subset of Node's CommonJS algorithm:
 *
 *   relative ("./x", "../x") and absolute ("/x") specifiers
 *     -> resolved against the requiring file's directory
 *   bare specifiers ("leftpad")
 *     -> looked up in node_modules/, walking up towards the filesystem root
 *
 * In both cases the candidate is tried as a file (exact, then with the
 * extensions in RESOLVE_EXTENSIONS appended) and then as a directory
 * (package.json "main", then index.<ext>).
 *
 * Not implemented: "exports"/"imports" maps, conditional exports, "browser"
 * field, self-referencing, symlink realpath resolution.
 */
#ifndef CB_RESOLVE_H
#define CB_RESOLVE_H

/* Extensions tried, in order, when a specifier has no usable extension. */
extern const char *const RESOLVE_EXTENSIONS[];

/*
 * Resolve `spec` as requested from a file in `importer_dir`.
 * Returns a malloc'd absolute, normalized path, or NULL when nothing matches.
 */
char *resolve_specifier(const char *spec, const char *importer_dir);

#endif /* CB_RESOLVE_H */
