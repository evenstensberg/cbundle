#include "util.h"

#include <errno.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ errors */

void cb_die(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("cbundle: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
    exit(EXIT_FAILURE);
}

void cb_warn(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fputs("cbundle: warning: ", stderr);
    vfprintf(stderr, fmt, ap);
    fputc('\n', stderr);
    va_end(ap);
}

/* -------------------------------------------------------------- allocation */

void *cb_malloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        cb_die("out of memory (requested %zu bytes)", n);
    return p;
}

void *cb_realloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        cb_die("out of memory (requested %zu bytes)", n);
    return q;
}

char *cb_strdup(const char *s)
{
    return cb_strndup(s, strlen(s));
}

char *cb_strndup(const char *s, size_t n)
{
    char *p = cb_malloc(n + 1);
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

/* ------------------------------------------------------------------ Buffer */

void buf_init(Buffer *b)
{
    b->data = cb_malloc(1);
    b->data[0] = '\0';
    b->len = 0;
    b->cap = 1;
}

void buf_free(Buffer *b)
{
    free(b->data);
    b->data = NULL;
    b->len = b->cap = 0;
}

/* Ensure there is room for `extra` more bytes plus the NUL terminator. */
static void buf_reserve(Buffer *b, size_t extra)
{
    size_t need = b->len + extra + 1;
    size_t cap;

    if (need <= b->cap)
        return;
    cap = b->cap ? b->cap : 1;
    while (cap < need)
        cap *= 2;
    b->data = cb_realloc(b->data, cap);
    b->cap = cap;
}

void buf_putc(Buffer *b, char c)
{
    buf_reserve(b, 1);
    b->data[b->len++] = c;
    b->data[b->len] = '\0';
}

void buf_put(Buffer *b, const char *s, size_t n)
{
    buf_reserve(b, n);
    memcpy(b->data + b->len, s, n);
    b->len += n;
    b->data[b->len] = '\0';
}

void buf_puts(Buffer *b, const char *s)
{
    buf_put(b, s, strlen(s));
}

void buf_printf(Buffer *b, const char *fmt, ...)
{
    va_list ap;
    int n;

    /* Measure first, then format directly into the buffer. */
    va_start(ap, fmt);
    n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0)
        cb_die("vsnprintf failed");

    buf_reserve(b, (size_t)n);
    va_start(ap, fmt);
    vsnprintf(b->data + b->len, (size_t)n + 1, fmt, ap);
    va_end(ap);
    b->len += (size_t)n;
}

void buf_put_json_string(Buffer *b, const char *s)
{
    buf_putc(b, '"');
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '"':  buf_puts(b, "\\\""); break;
        case '\\': buf_puts(b, "\\\\"); break;
        case '\b': buf_puts(b, "\\b");  break;
        case '\f': buf_puts(b, "\\f");  break;
        case '\n': buf_puts(b, "\\n");  break;
        case '\r': buf_puts(b, "\\r");  break;
        case '\t': buf_puts(b, "\\t");  break;
        default:
            if (c < 0x20)
                buf_printf(b, "\\u%04x", c);
            else
                buf_putc(b, (char)c); /* UTF-8 bytes pass through unchanged */
        }
    }
    buf_putc(b, '"');
}

/* -------------------------------------------------------------------- file */

char *cb_read_file(const char *path, size_t *len_out)
{
    FILE *f;
    Buffer b;
    char chunk[8192];
    size_t n;

    f = fopen(path, "rb");
    if (!f)
        return NULL;

    buf_init(&b);
    while ((n = fread(chunk, 1, sizeof chunk, f)) > 0)
        buf_put(&b, chunk, n);

    if (ferror(f)) {
        fclose(f);
        buf_free(&b);
        return NULL;
    }
    fclose(f);

    if (len_out)
        *len_out = b.len;
    return b.data; /* ownership transfers to the caller */
}

int cb_write_file(const char *path, const char *data, size_t len)
{
    FILE *f = fopen(path, "wb");
    if (!f)
        return -1;
    if (len && fwrite(data, 1, len, f) != len) {
        fclose(f);
        return -1;
    }
    return fclose(f) == 0 ? 0 : -1;
}
