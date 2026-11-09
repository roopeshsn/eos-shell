#include "cmdtree.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

typedef struct cmd_node {
    char *word;
    char *help;
    int is_arg;
    int is_rest;
    cmd_handler handler;
    struct cmd_node **kids;
    int nkids, cap;
} cmd_node;

static cmd_node *roots[MODE_COUNT];

static void *xmalloc(size_t n)
{
    void *p = calloc(1, n);
    if (!p) {
        perror("calloc");
        exit(1);
    }
    return p;
}

static char *xstrdup(const char *s)
{
    char *p = strdup(s);
    if (!p) {
        perror("strdup");
        exit(1);
    }
    return p;
}

static cmd_node *node_new(const char *word, int is_arg, int is_rest)
{
    cmd_node *n = xmalloc(sizeof *n);
    n->word = xstrdup(word);
    n->is_arg = is_arg;
    n->is_rest = is_rest;
    return n;
}

static void node_free(cmd_node *n)
{
    for (int i = 0; i < n->nkids; i++)
        node_free(n->kids[i]);
    free(n->kids);
    free(n->word);
    free(n->help);
    free(n);
}

void cmd_tree_free(void)
{
    for (int m = 0; m < MODE_COUNT; m++) {
        if (roots[m])
            node_free(roots[m]);
        roots[m] = NULL;
    }
}

/* Literals sorted alphabetically, argument nodes last. */
static void node_add_kid(cmd_node *parent, cmd_node *kid)
{
    if (parent->nkids == parent->cap) {
        parent->cap = parent->cap ? parent->cap * 2 : 4;
        parent->kids = realloc(parent->kids, (size_t)parent->cap * sizeof *parent->kids);
        if (!parent->kids) {
            perror("realloc");
            exit(1);
        }
    }
    int pos = parent->nkids;
    if (!kid->is_arg) {
        pos = 0;
        while (pos < parent->nkids && !parent->kids[pos]->is_arg &&
               strcasecmp(parent->kids[pos]->word, kid->word) < 0)
            pos++;
    }
    memmove(&parent->kids[pos + 1], &parent->kids[pos],
            (size_t)(parent->nkids - pos) * sizeof *parent->kids);
    parent->kids[pos] = kid;
    parent->nkids++;
}

static cmd_node *node_find_exact(const cmd_node *parent, const char *word, int is_arg)
{
    for (int i = 0; i < parent->nkids; i++) {
        cmd_node *k = parent->kids[i];
        if (k->is_arg == is_arg && strcmp(k->word, word) == 0)
            return k;
    }
    return NULL;
}

static void register_one(cmd_node *root, const char *path, const char *help, cmd_handler handler)
{
    char *copy = xstrdup(path);
    cmd_node *cur = root;
    char *save = NULL;

    for (char *tok = strtok_r(copy, " ", &save); tok; tok = strtok_r(NULL, " ", &save)) {
        size_t len = strlen(tok);
        int is_arg = tok[0] == '<' && tok[len - 1] == '>';
        int is_rest = 0;
        if (is_arg && len > 5 && strcmp(tok + len - 4, "...>") == 0) {
            is_rest = 1;
            tok[len - 4] = '>';
            tok[len - 3] = '\0';
        }
        cmd_node *next = node_find_exact(cur, tok, is_arg);
        if (!next) {
            next = node_new(tok, is_arg, is_rest);
            node_add_kid(cur, next);
        }
        cur = next;
    }
    if (help) {
        free(cur->help);
        cur->help = xstrdup(help);
    }
    if (handler)
        cur->handler = handler;
    free(copy);
}

void cmd_register(unsigned modes, const char *path, const char *help, cmd_handler handler)
{
    for (int m = 0; m < MODE_COUNT; m++) {
        if (!(modes & (1u << m)))
            continue;
        if (!roots[m])
            roots[m] = node_new("", 0, 0);
        register_one(roots[m], path, help, handler);
    }
}

/* ---- tokenizing ---- */

static int tokenize(const char *line, char ***out, int *trailing_space)
{
    int cap = 8, n = 0;
    char **toks = xmalloc((size_t)cap * sizeof *toks);
    const char *p = line;

    *trailing_space = 0;
    while (*p) {
        while (*p && isspace((unsigned char)*p))
            p++;
        if (!*p)
            break;
        const char *start = p;
        while (*p && !isspace((unsigned char)*p))
            p++;
        if (n == cap) {
            cap *= 2;
            toks = realloc(toks, (size_t)cap * sizeof *toks);
            if (!toks) {
                perror("realloc");
                exit(1);
            }
        }
        toks[n] = xmalloc((size_t)(p - start) + 1);
        memcpy(toks[n], start, (size_t)(p - start));
        n++;
    }
    if (n > 0 && isspace((unsigned char)line[strlen(line) - 1]))
        *trailing_space = 1;
    *out = toks;
    return n;
}

static void free_tokens(char **toks, int n)
{
    for (int i = 0; i < n; i++)
        free(toks[i]);
    free(toks);
}

/* ---- matching ---- */

static const cmd_node *find_child(const cmd_node *n, const char *tok, cmd_status *st)
{
    const cmd_node *exact = NULL, *pref = NULL, *arg = NULL;
    int npref = 0;
    size_t len = strlen(tok);

    for (int i = 0; i < n->nkids; i++) {
        const cmd_node *k = n->kids[i];
        if (k->is_arg) {
            arg = k;
        } else if (strcasecmp(k->word, tok) == 0) {
            exact = k;
        } else if (strncasecmp(k->word, tok, len) == 0) {
            pref = k;
            npref++;
        }
    }
    *st = CMD_OK;
    if (exact)
        return exact;
    if (npref == 1)
        return pref;
    if (npref > 1) {
        *st = CMD_AMBIGUOUS;
        return NULL;
    }
    if (arg)
        return arg;
    *st = CMD_INVALID;
    return NULL;
}

cmd_status cmd_execute(cli_ctx *ctx, const char *line)
{
    char **toks;
    int trailing, ntok = tokenize(line, &toks, &trailing);
    cmd_status st = CMD_OK;

    if (ntok == 0) {
        free(toks);
        return CMD_OK;
    }

    const cmd_node *n = roots[ctx->mode];
    const char **av = xmalloc(((size_t)ntok + 1) * sizeof *av);
    int bad = -1;

    for (int i = 0; n && i < ntok; i++) {
        const cmd_node *k = find_child(n, toks[i], &st);
        if (!k) {
            bad = i;
            n = NULL;
            break;
        }
        if (k->is_rest) {
            for (int j = i; j < ntok; j++)
                av[j] = toks[j];
            n = k;
            break;
        }
        av[i] = k->is_arg ? toks[i] : k->word;
        n = k;
    }

    if (!n) {
        if (st == CMD_AMBIGUOUS)
            printf("%% Ambiguous command (at token %d: '%s')\n", bad + 1, toks[bad]);
        else
            printf("%% Invalid input (at token %d: '%s')\n", bad + 1, toks[bad]);
    } else if (!n->handler) {
        st = CMD_INCOMPLETE;
        printf("%% Incomplete command\n");
    } else {
        st = n->handler(ctx, ntok, av) == 0 ? CMD_OK : CMD_INVALID;
    }

    free(av);
    free_tokens(toks, ntok);
    return st;
}

/* Walk fully-typed words; returns NULL if the path is invalid or ambiguous. */
static const cmd_node *walk_complete(const cli_ctx *ctx, char **toks, int n)
{
    const cmd_node *cur = roots[ctx->mode];
    cmd_status st;

    for (int i = 0; cur && i < n; i++) {
        cur = find_child(cur, toks[i], &st);
        if (cur && cur->is_rest)
            break;
    }
    return cur;
}

static int split_partial(char **toks, int ntok, int trailing, const char **partial)
{
    if (trailing || ntok == 0) {
        *partial = "";
        return ntok;
    }
    *partial = toks[ntok - 1];
    return ntok - 1;
}

char **cmd_complete(const cli_ctx *ctx, const char *line, int *count)
{
    char **toks;
    int trailing, ntok = tokenize(line, &toks, &trailing);
    const char *partial;
    int ndone = split_partial(toks, ntok, trailing, &partial);
    const cmd_node *n = walk_complete(ctx, toks, ndone);
    char **out = NULL;
    int cnt = 0;

    if (n && !n->is_rest) {
        out = xmalloc(((size_t)n->nkids + 1) * sizeof *out);
        for (int i = 0; i < n->nkids; i++) {
            const cmd_node *k = n->kids[i];
            if (!k->is_arg && strncasecmp(k->word, partial, strlen(partial)) == 0)
                out[cnt++] = xstrdup(k->word);
        }
    }
    free_tokens(toks, ntok);
    *count = cnt;
    return out;
}

void cmd_free_list(char **list, int count)
{
    for (int i = 0; i < count; i++)
        free(list[i]);
    free(list);
}

void cmd_help(const cli_ctx *ctx, const char *line, FILE *out)
{
    char **toks;
    int trailing, ntok = tokenize(line, &toks, &trailing);
    const char *partial;
    int ndone = split_partial(toks, ntok, trailing, &partial);
    const cmd_node *n = walk_complete(ctx, toks, ndone);

    if (!n) {
        fprintf(out, "%% Invalid input\n");
    } else if (n->is_rest) {
        fprintf(out, "  %-14s %s\n", n->word, n->help ? n->help : "");
    } else {
        size_t plen = strlen(partial);
        if (plen == 0 && n->handler) {
            fprintf(out, "  %-14s %s\n", "<cr>", "");
            fprintf(out, "  %-14s %s\n", "|", "Output modifiers");
        }
        for (int i = 0; i < n->nkids; i++) {
            const cmd_node *k = n->kids[i];
            if (k->is_arg || strncasecmp(k->word, partial, plen) == 0)
                fprintf(out, "  %-14s %s\n", k->word, k->help ? k->help : "");
        }
    }
    free_tokens(toks, ntok);
}
