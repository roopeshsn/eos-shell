#include "cli.h"

#include <stdio.h>
#include <readline/history.h>
#include <readline/readline.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmdtree.h"
#include "pipe.h"

static cli_ctx *g_ctx;
static char **g_matches;
static int g_nmatches;

void cli_prompt(const cli_ctx *ctx, char *buf, size_t n)
{
    switch (ctx->mode) {
    case MODE_EXEC:
        snprintf(buf, n, "%s>", ctx->hostname);
        break;
    case MODE_PRIV:
        snprintf(buf, n, "%s#", ctx->hostname);
        break;
    case MODE_CONFIG:
        snprintf(buf, n, "%s(config)#", ctx->hostname);
        break;
    case MODE_CONFIG_IF:
        snprintf(buf, n, "%s(config-if-%s)#", ctx->hostname, ctx->iface);
        break;
    default:
        snprintf(buf, n, "%s>", ctx->hostname);
    }
}

static char *match_generator(const char *text, int state)
{
    static int idx;
    (void)text;

    if (state == 0) {
        idx = 0;
        cmd_free_list(g_matches, g_nmatches);
        char *pre = strndup(rl_line_buffer, (size_t)rl_point);
        g_matches = cmd_complete(g_ctx, pre ? pre : "", &g_nmatches);
        free(pre);
    }
    if (idx < g_nmatches)
        return strdup(g_matches[idx++]);
    return NULL;
}

static char **attempt_completion(const char *text, int start, int end)
{
    (void)start;
    (void)end;
    rl_attempted_completion_over = 1;
    return rl_completion_matches(text, match_generator);
}

static int on_question(int count, int key)
{
    (void)count;
    (void)key;
    char *pre = strndup(rl_line_buffer, (size_t)rl_point);
    fputs("?\n", stdout);
    if (!pipe_help(pre ? pre : "", stdout))
        cmd_help(g_ctx, pre ? pre : "", stdout);
    free(pre);
    rl_on_new_line();
    rl_redisplay();
    return 0;
}

void cli_init(cli_ctx *ctx)
{
    g_ctx = ctx;
    if (isatty(STDIN_FILENO)) {
        rl_attempted_completion_function = attempt_completion;
        rl_completer_word_break_characters = " \t\n";
        rl_bind_key('?', on_question);
    }
}

char *cli_readline(cli_ctx *ctx)
{
    char prompt[160];
    cli_prompt(ctx, prompt, sizeof prompt);

    if (isatty(STDIN_FILENO))
        return readline(prompt);

    /* Piped input: echo prompt and command so transcripts read naturally. */
    char *line = NULL;
    size_t cap = 0;
    ssize_t len = getline(&line, &cap, stdin);
    if (len < 0) {
        free(line);
        return NULL;
    }
    while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r'))
        line[--len] = '\0';
    printf("%s%s\n", prompt, line);
    return line;
}
