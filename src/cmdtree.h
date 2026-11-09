#ifndef CMDTREE_H
#define CMDTREE_H

#include <stdio.h>
#include "ctx.h"

typedef int (*cmd_handler)(cli_ctx *ctx, int argc, const char *const *argv);

typedef enum {
    CMD_OK,
    CMD_INCOMPLETE,
    CMD_INVALID,
    CMD_AMBIGUOUS
} cmd_status;

/*
 * Register a command in every mode in the `modes` bitmask.
 * `path` is space-separated tokens. "<name>" is a single free-form argument,
 * "<text...>" consumes the rest of the line. `help` describes the last token.
 * A NULL handler only declares/describes an intermediate keyword.
 */
void cmd_register(unsigned modes, const char *path, const char *help, cmd_handler handler);

/* Parse and run one line in ctx->mode. Prints EOS-style errors to stdout. */
cmd_status cmd_execute(cli_ctx *ctx, const char *line);

/* Completion candidates for the partial last word of `line`. Free with cmd_free_list. */
char **cmd_complete(const cli_ctx *ctx, const char *line, int *count);
void cmd_free_list(char **list, int count);

/* '?' help for the text typed so far. */
void cmd_help(const cli_ctx *ctx, const char *line, FILE *out);

void cmd_tree_free(void);

#endif
