#include <time.h>

#include "cmds.h"
#include "cmdtree.h"
#include "sys/sys.h"

static int do_show_clock(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    char buf[64];
    time_t now = time(NULL);
    struct tm tm;
    localtime_r(&now, &tm);
    strftime(buf, sizeof buf, "%a %b %e %H:%M:%S %Y", &tm);
    fprintf(ctx->out, "%s\n", buf);
    return 0;
}

static int do_show_hostname(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    fprintf(ctx->out, "Hostname: %s\n", ctx->hostname);
    return 0;
}

static int do_show_version(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_info si;
    char up[96];

    sys_get_info(&si);
    sys_format_uptime(si.uptime_secs, up, sizeof up);

    fprintf(ctx->out, "Hostname:         %s\n", ctx->hostname);
    fprintf(ctx->out, "Hardware model:   %s\n", si.model[0] ? si.model : "unknown");
    if (si.serial[0])
        fprintf(ctx->out, "Serial number:    %s\n", si.serial);
    fprintf(ctx->out, "Operating system: %s\n", si.os[0] ? si.os : "unknown");
    fprintf(ctx->out, "Kernel:           %s\n", si.kernel);
    fprintf(ctx->out, "Architecture:     %s\n", si.arch);
    fprintf(ctx->out, "Uptime:           %s\n", up);
    fprintf(ctx->out, "Load average:     %.2f, %.2f, %.2f\n", si.load[0], si.load[1], si.load[2]);
    fprintf(ctx->out, "Total memory:     %llu MB\n", si.mem_total >> 20);
    fprintf(ctx->out, "Free memory:      %llu MB\n", si.mem_free >> 20);
    return 0;
}

void cmds_register_show(void)
{
    cmd_register(M_SHOW, "show", "Show running system information", NULL);
    cmd_register(M_SHOW, "show clock", "Display the system clock", do_show_clock);
    cmd_register(M_SHOW, "show hostname", "Show the shell hostname", do_show_hostname);
    cmd_register(M_SHOW, "show version", "Host hardware and software details", do_show_version);
}
