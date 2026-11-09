#ifndef PIPE_H
#define PIPE_H

#include <stdio.h>
#include "cmdtree.h"

/*
 * Run `line`, which may end in output filters: cmd | include RE | exclude RE
 * | grep [-i] [-v] RE | begin RE | count. Filters chain left to right.
 * Double quotes protect a '|' inside a pattern.
 */
cmd_status pipe_execute(cli_ctx *ctx, const char *line);

/* '?' help when the cursor is after a '|'. Returns 1 if it printed help. */
int pipe_help(const char *line, FILE *out);

#endif
