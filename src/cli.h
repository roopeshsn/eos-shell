#ifndef CLI_H
#define CLI_H

#include "ctx.h"

void cli_init(cli_ctx *ctx);
void cli_prompt(const cli_ctx *ctx, char *buf, size_t n);

/* Returns a malloc'd line, or NULL on EOF. Works for ttys and piped input. */
char *cli_readline(cli_ctx *ctx);

#endif
