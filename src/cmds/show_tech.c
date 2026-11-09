#include "cmds.h"
#include "cmdtree.h"

static const char *const tech_commands[] = {
    "show clock",
    "show version",
    "show environment",
    "show users",
    "show interfaces status",
    "show interfaces",
    "show interfaces counters",
    "show ip interface brief",
    "show ip route",
    "show ip route summary",
    "show ipv6 route",
    "show arp",
    "show processes top",
    "show logging 100",
};

static int do_show_tech(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    for (size_t i = 0; i < sizeof tech_commands / sizeof *tech_commands; i++) {
        fprintf(ctx->out, "\n------------- %s -------------\n", tech_commands[i]);
        cmd_execute(ctx, tech_commands[i]);
    }
    return 0;
}

void cmds_register_show_tech(void)
{
    cmd_register(M_SHOW, "show tech-support", "Diagnostic output of the main show commands", do_show_tech);
}
