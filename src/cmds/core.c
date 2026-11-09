#include "cmds.h"

#include "cmdtree.h"

static int do_enable(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    ctx->mode = MODE_PRIV;
    return 0;
}

static int do_disable(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    ctx->mode = MODE_EXEC;
    return 0;
}

static int do_configure(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    ctx->mode = MODE_CONFIG;
    return 0;
}

static int do_end(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    ctx->mode = MODE_PRIV;
    return 0;
}

static int do_exit(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    switch (ctx->mode) {
    case MODE_CONFIG_IF:
        ctx->mode = MODE_CONFIG;
        break;
    case MODE_CONFIG:
        ctx->mode = MODE_PRIV;
        break;
    default:
        ctx->quit = 1;
    }
    return 0;
}

static int do_quit(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    ctx->quit = 1;
    return 0;
}

void cmds_register_core(void)
{
    cmd_register(M_EXEC, "enable", "Turn on privileged commands", do_enable);
    cmd_register(M_PRIV, "disable", "Turn off privileged commands", do_disable);
    cmd_register(M_PRIV, "configure", "Enter configuration mode", do_configure);
    cmd_register(M_PRIV, "configure terminal", "Configure from the terminal", do_configure);
    cmd_register(M_CONFIG | M_CONFIG_IF, "end", "Return to privileged exec mode", do_end);
    cmd_register(M_ALL, "exit", "Exit from the current mode", do_exit);
    cmd_register(M_ALL, "quit", "Exit the shell", do_quit);
}
