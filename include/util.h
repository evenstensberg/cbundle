/*
 * util.h -- small helpers shared by the whole bundler:
 *           allocation wrappers that abort on OOM, a growable byte buffer,
 *           and whole-file I/O.
 */
#ifndef CB_UTIL_H
#define CB_UTIL_H

#include <stddef.h>
#include <stdio.h>

/* Marks a function that never returns, so both the compiler and static
 * analyzers know that control stops at a cb_die() call. */
#if defined(__GNUC__) || defined(__clang__)
#  define CB_NORETURN __attribute__((noreturn))
#  define CB_PRINTF(fmt, first) __attribute__((format(printf, fmt, first)))
#else
#  define CB_NORETURN
#  define CB_PRINTF(fmt, first)
#endif

/* Print "cbundle: ..." to stderr and exit(EXIT_FAILURE). */
CB_NORETURN CB_PRINTF(1, 2) void cb_die(const char *fmt, ...);
/* Print "cbundle: warning: ..." to stderr and return. */
CB_PRINTF(1, 2) void cb_warn(const char *fmt, ...);

/* Allocation wrappers: never return NULL (they cb_die instead), which keeps
 * every call site free of error handling noise. */
void *cb_malloc(size_t n);
void *cb_realloc(void *p, size_t n);
char *cb_strdup(const char *s);
char *cb_strndup(const char *s, size_t n);

/* ---------------------------------------------------------------------------
 * Buffer: an append-only, NUL-terminated, growable byte buffer.
 * `data` is always NUL-terminated, so it can be passed to C string APIs.
 * ------------------------------------------------------------------------- */
typedef struct {
    char  *data;
    size_t len; /* bytes used, excluding the terminating NUL */
    size_t cap; /* bytes allocated, including room for the NUL */
} Buffer;

void buf_init(Buffer *b);
void buf_free(Buffer *b);
void buf_putc(Buffer *b, char c);
void buf_put(Buffer *b, const char *s, size_t n);
void buf_puts(Buffer *b, const char *s);
CB_PRINTF(2, 3) void buf_printf(Buffer *b, const char *fmt, ...);
/* Append `s` as a double-quoted, escaped JSON string literal. */
void buf_put_json_string(Buffer *b, const char *s);

/* ---------------------------------------------------------------------------
 * File I/O
 * ------------------------------------------------------------------------- */

/* Read a whole file. Returns a NUL-terminated, malloc'd buffer (caller frees)
 * and stores the byte length in *len_out. Returns NULL if the file cannot be
 * opened or read. */
char *cb_read_file(const char *path, size_t *len_out);

/* Write `len` bytes to `path`. Returns 0 on success, -1 on failure. */
int cb_write_file(const char *path, const char *data, size_t len);

#endif /* CB_UTIL_H */
