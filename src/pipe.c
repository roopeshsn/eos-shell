#include "pipe.h"

#include <ctype.h>
#include <regex.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef enum { F_INCLUDE, F_EXCLUDE, F_BEGIN, F_COUNT } filter_type;

typedef struct {
    filter_type type;
    regex_t re;
    int has_re;
} filter;

static const struct {
    const char *name;
    const char *help;
} filter_help[] = {
    {"begin", "Start output at the first line matching a regex"},
    {"count", "Count the number of output lines"},
    {"exclude", "Drop lines matching a regex"},
    {"grep", "Match lines: grep [-i] [-v] REGEX"},
    {"include", "Keep only lines matching a regex"},
};

static char *dup_range(const char *s, size_t len)
{
    char *p = malloc(len + 1);
    if (!p) {
        perror("malloc");
        exit(1);
    }
    memcpy(p, s, len);
    p[len] = '\0';
    return p;
}

/* Split on '|' outside double quotes. Returns segment count. */
static int split_segments(const char *line, char ***out)
{
    int cap = 4, n = 0, quoted = 0;
    char **segs = malloc((size_t)cap * sizeof *segs);
    const char *start = line;

    if (!segs) {
        perror("malloc");
        exit(1);
    }
    for (const char *p = line;; p++) {
        if (*p == '"')
            quoted = !quoted;
        if ((*p == '|' && !quoted) || *p == '\0') {
            if (n == cap) {
                cap *= 2;
                segs = realloc(segs, (size_t)cap * sizeof *segs);
                if (!segs) {
                    perror("realloc");
                    exit(1);
                }
            }
            segs[n++] = dup_range(start, (size_t)(p - start));
            start = p + 1;
            if (*p == '\0')
                break;
        }
    }
    *out = segs;
    return n;
}

static void free_segments(char **segs, int n)
{
    for (int i = 0; i < n; i++)
        free(segs[i]);
    free(segs);
}

static char *trim_inplace(char *s)
{
    while (isspace((unsigned char)*s))
        s++;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        *--end = '\0';
    return s;
}

static void strip_quotes(char *s)
{
    size_t len = strlen(s);
    if (len >= 2 && s[0] == '"' && s[len - 1] == '"') {
        memmove(s, s + 1, len - 2);
        s[len - 2] = '\0';
    }
}

static void filter_free(filter *f)
{
    if (f->has_re)
        regfree(&f->re);
    f->has_re = 0;
}

/* Parses one "keyword args" segment. Returns 0 on success, else prints an error. */
static int filter_parse(const char *seg_in, filter *f)
{
    char *seg = dup_range(seg_in, strlen(seg_in));
    char *s = trim_inplace(seg);
    char *rest = s;
    int rc = -1, cflags = REG_EXTENDED | REG_NOSUB, invert = 0;

    memset(f, 0, sizeof *f);
    while (*rest && !isspace((unsigned char)*rest))
        rest++;
    size_t klen = (size_t)(rest - s);
    char kw[16] = "";
    if (klen > 0 && klen < sizeof kw) {
        memcpy(kw, s, klen);
        kw[klen] = '\0';
    }
    rest = trim_inplace(rest);

    int grep = 0;
    if (klen == 0) {
        printf("%% Incomplete command\n");
        goto done;
    } else if (klen >= sizeof kw) {
        printf("%% Invalid input (after '|': '%s')\n", s);
        goto done;
    } else if (strncasecmp("include", kw, klen) == 0) {
        f->type = F_INCLUDE;
    } else if (strncasecmp("exclude", kw, klen) == 0) {
        f->type = F_EXCLUDE;
    } else if (strncasecmp("begin", kw, klen) == 0) {
        f->type = F_BEGIN;
    } else if (strncasecmp("count", kw, klen) == 0) {
        f->type = F_COUNT;
    } else if (strncasecmp("grep", kw, klen) == 0) {
        f->type = F_INCLUDE;
        grep = 1;
    } else {
        printf("%% Invalid input (after '|': '%s')\n", s);
        goto done;
    }

    if (f->type == F_COUNT) {
        if (*rest) {
            printf("%% Invalid input (after '|': '%s')\n", rest);
            goto done;
        }
        rc = 0;
        goto done;
    }

    while (grep && rest[0] == '-' && rest[1] && !isspace((unsigned char)rest[1])) {
        char *flags = rest + 1;
        while (*flags && !isspace((unsigned char)*flags)) {
            if (*flags == 'i') {
                cflags |= REG_ICASE;
            } else if (*flags == 'v') {
                invert = !invert;
            } else {
                printf("%% Invalid grep option: -%c\n", *flags);
                goto done;
            }
            flags++;
        }
        rest = trim_inplace(flags);
    }
    if (invert)
        f->type = F_EXCLUDE;

    strip_quotes(rest);
    if (!*rest) {
        printf("%% Incomplete command\n");
        goto done;
    }
    int err = regcomp(&f->re, rest, cflags);
    if (err) {
        printf("%% Invalid regular expression: '%s'\n", rest);
        goto done;
    }
    f->has_re = 1;
    rc = 0;

done:
    free(seg);
    return rc;
}

static int line_matches(const filter *f, const char *line)
{
    return regexec(&f->re, line, 0, NULL, 0) == 0;
}

/* Applies one filter to `in`, returning a new malloc'd buffer. */
static char *filter_apply(const filter *f, const char *in)
{
    size_t cap = strlen(in) + 64, len = 0;
    char *out = malloc(cap);
    long nlines = 0;
    int started = 0;

    if (!out) {
        perror("malloc");
        exit(1);
    }
    out[0] = '\0';

    const char *p = in;
    while (*p) {
        const char *nl = strchr(p, '\n');
        size_t llen = nl ? (size_t)(nl - p) : strlen(p);
        char *line = dup_range(p, llen);
        int keep = 0;

        switch (f->type) {
        case F_INCLUDE:
            keep = line_matches(f, line);
            break;
        case F_EXCLUDE:
            keep = !line_matches(f, line);
            break;
        case F_BEGIN:
            if (!started && line_matches(f, line))
                started = 1;
            keep = started;
            break;
        case F_COUNT:
            nlines++;
            break;
        }
        if (keep) {
            memcpy(out + len, line, llen);
            len += llen;
            out[len++] = '\n';
            out[len] = '\0';
        }
        free(line);
        p += llen + (nl ? 1 : 0);
    }
    if (f->type == F_COUNT)
        snprintf(out, cap, "Number of lines in output: %ld\n", nlines);
    return out;
}

cmd_status pipe_execute(cli_ctx *ctx, const char *line)
{
    if (!strchr(line, '|'))
        return cmd_execute(ctx, line);

    char **segs;
    int nseg = split_segments(line, &segs);
    int nfilters = nseg - 1;
    filter *filters = calloc((size_t)nfilters, sizeof *filters);
    cmd_status st = CMD_INVALID;
    int parsed = 0;

    if (!filters) {
        perror("calloc");
        exit(1);
    }
    for (; parsed < nfilters; parsed++) {
        if (filter_parse(segs[parsed + 1], &filters[parsed]) != 0)
            goto out;
    }

    char *buf = NULL;
    size_t size = 0;
    FILE *mem = open_memstream(&buf, &size);
    if (!mem) {
        perror("open_memstream");
        goto out;
    }
    FILE *saved = ctx->out;
    ctx->out = mem;
    st = cmd_execute(ctx, segs[0]);
    ctx->out = saved;
    fclose(mem);

    char *text = buf;
    if (st != CMD_OK && size == 0) {
        free(text);
        goto out;
    }
    for (int i = 0; i < nfilters; i++) {
        char *next = filter_apply(&filters[i], text);
        free(text);
        text = next;
    }
    fputs(text, ctx->out);
    free(text);

out:
    for (int i = 0; i < parsed; i++)
        filter_free(&filters[i]);
    free(filters);
    free_segments(segs, nseg);
    return st;
}

int pipe_help(const char *line, FILE *out)
{
    const char *bar = strrchr(line, '|');
    if (!bar)
        return 0;

    const char *seg = bar + 1;
    while (isspace((unsigned char)*seg))
        seg++;
    if (strchr(seg, ' ') || strchr(seg, '\t'))
        return 1; /* already past the filter keyword: nothing to suggest */

    size_t plen = strlen(seg);
    for (size_t i = 0; i < sizeof filter_help / sizeof *filter_help; i++) {
        if (strncasecmp(filter_help[i].name, seg, plen) == 0)
            fprintf(out, "  %-14s %s\n", filter_help[i].name, filter_help[i].help);
    }
    return 1;
}
