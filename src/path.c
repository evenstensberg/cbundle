#include "path.h"
#include "util.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

char *path_dirname(const char *p)
{
    const char *slash = strrchr(p, '/');

    if (!slash)
        return cb_strdup(".");
    if (slash == p)
        return cb_strdup("/");
    return cb_strndup(p, (size_t)(slash - p));
}

char *path_join(const char *a, const char *b)
{
    Buffer out;

    if (b[0] == '/')
        return cb_strdup(b);

    buf_init(&out);
    buf_puts(&out, a);
    if (out.len > 0 && out.data[out.len - 1] != '/')
        buf_putc(&out, '/');
    buf_puts(&out, b);
    return out.data;
}

char *path_normalize(const char *p)
{
    int absolute = (p[0] == '/');
    const char **segs;
    size_t nsegs = 0, cap = 8, i;
    const char *s = p;
    Buffer out;

    segs = cb_malloc(cap * sizeof *segs);

    /* Walk the path segment by segment, maintaining a stack of kept segments.
     * A ".." pops the stack unless the top is itself a ".." (a relative path
     * may legitimately start with leading "..", e.g. "../../x"). */
    while (*s) {
        const char *end;
        size_t len;

        while (*s == '/')
            s++;
        if (!*s)
            break;
        end = strchr(s, '/');
        len = end ? (size_t)(end - s) : strlen(s);

        if (len == 1 && s[0] == '.') {
            /* "." contributes nothing */
        } else if (len == 2 && s[0] == '.' && s[1] == '.') {
            if (nsegs > 0 && !(strlen(segs[nsegs - 1]) == 2 &&
                               strncmp(segs[nsegs - 1], "..", 2) == 0)) {
                nsegs--;
            } else if (!absolute) {
                if (nsegs == cap)
                    segs = cb_realloc(segs, (cap *= 2) * sizeof *segs);
                segs[nsegs++] = s; /* keep a leading ".." */
            }
            /* An absolute path cannot climb above "/", so ".." is dropped. */
        } else {
            if (nsegs == cap)
                segs = cb_realloc(segs, (cap *= 2) * sizeof *segs);
            segs[nsegs++] = s;
        }
        s = end ? end + 1 : s + len;
    }

    buf_init(&out);
    if (absolute)
        buf_putc(&out, '/');
    for (i = 0; i < nsegs; i++) {
        const char *seg = segs[i];
        const char *end = strchr(seg, '/');
        size_t len = end ? (size_t)(end - seg) : strlen(seg);

        if (i > 0)
            buf_putc(&out, '/');
        buf_put(&out, seg, len);
    }
    free(segs);

    if (out.len == 0)
        buf_puts(&out, absolute ? "/" : ".");
    return out.data;
}

char *path_absolute(const char *p)
{
    char cwd[4096];
    char *joined, *norm;

    if (p[0] == '/')
        return path_normalize(p);

    if (!getcwd(cwd, sizeof cwd))
        cb_die("cannot determine the current working directory");

    joined = path_join(cwd, p);
    norm = path_normalize(joined);
    free(joined);
    return norm;
}

char *path_relative(const char *base, const char *p)
{
    size_t blen = strlen(base);

    /* Only the easy, common case is handled: `p` sits inside `base`. Anything
     * else keeps its absolute form, which is still correct, just longer. */
    if (blen > 0 && strncmp(base, p, blen) == 0) {
        if (p[blen] == '/')
            return cb_strdup(p + blen + 1);
        if (p[blen] == '\0')
            return cb_strdup(".");
    }
    return cb_strdup(p);
}

int path_is_relative_spec(const char *spec)
{
    if (spec[0] == '/')
        return 1;
    if (spec[0] == '.' && (spec[1] == '/' || spec[1] == '\0'))
        return 1;
    if (spec[0] == '.' && spec[1] == '.' && (spec[2] == '/' || spec[2] == '\0'))
        return 1;
    return 0;
}

int path_is_file(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISREG(st.st_mode);
}

int path_is_dir(const char *p)
{
    struct stat st;
    return stat(p, &st) == 0 && S_ISDIR(st.st_mode);
}

const char *path_ext(const char *p)
{
    const char *slash = strrchr(p, '/');
    const char *dot = strrchr(p, '.');

    if (!dot || (slash && dot < slash) || dot == p || (slash && dot == slash + 1))
        return "";
    return dot;
}
