#include <stdio.h>
#include <readline/history.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cli.h"
#include "cmds/cmds.h"
#include "cmdtree.h"
#include "history.h"
#include "pipe.h"

static void init_hostname(cli_ctx *ctx)
{
    const char *override = getenv("EOSH_HOSTNAME");
    if (override) {
        snprintf(ctx->hostname, sizeof ctx->hostname, "%s", override);
        return;
    }
    if (gethostname(ctx->hostname, sizeof ctx->hostname) != 0)
        strcpy(ctx->hostname, "localhost");
    ctx->hostname[sizeof ctx->hostname - 1] = '\0';
    char *dot = strchr(ctx->hostname, '.');
    if (dot)
        *dot = '\0';
}

static char *trim(char *s)
{
    while (*s == ' ' || *s == '\t')
        s++;
    char *end = s + strlen(s);
    while (end > s && (end[-1] == ' ' || end[-1] == '\t'))
        *--end = '\0';
    return s;
}

int main(void)
{
    cli_ctx ctx = {.mode = MODE_EXEC, .out = stdout};
    init_hostname(&ctx);

    cmds_register_core();
    cmds_register_show();
    cmds_register_show_interfaces();
    cmds_register_show_routing();
    cmds_register_show_processes();
    cmds_register_show_system();
    cmds_register_show_tech();
    cli_init(&ctx);

    char *line;
    while (!ctx.quit && (line = cli_readline(&ctx)) != NULL) {
        char *cmd = trim(line);
        if (*cmd) {
            hist_add(cmd);
            if (isatty(STDIN_FILENO))
                add_history(cmd);
            pipe_execute(&ctx, cmd);
            fflush(stdout);
        }
        free(line);
    }

    hist_free();
    cmd_tree_free();
    return 0;
}
