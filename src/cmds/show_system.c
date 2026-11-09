#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cmds.h"
#include "cmdtree.h"
#include "history.h"
#include "sys/sys.h"

#define LOG_DEFAULT_LINES 100
#define LOG_MAX_LINES 1000

static void format_idle(long long secs, char *buf, size_t n)
{
    if (secs < 0)
        snprintf(buf, n, "-");
    else
        snprintf(buf, n, "%lld:%02lld:%02lld", secs / 3600, secs % 3600 / 60, secs % 60);
}

static int do_show_users(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_user *u;
    int n;

    if (sys_get_users(&u, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the user list\n");
        return 1;
    }
    fprintf(ctx->out, "   %-12s %-16s %-20s %-10s %s\n", "Line", "User", "Host", "Idle", "Login time");
    for (int i = 0; i < n; i++) {
        char idle[24], when[32];
        time_t t = (time_t)u[i].login;
        struct tm tm;

        format_idle(u[i].idle_secs, idle, sizeof idle);
        if (localtime_r(&t, &tm))
            strftime(when, sizeof when, "%b %e %H:%M", &tm);
        else
            snprintf(when, sizeof when, "-");
        fprintf(ctx->out, "%c  %-12.12s %-16.16s %-20.20s %-10s %s\n", u[i].current ? '*' : ' ', u[i].line, u[i].user,
                u[i].host[0] ? u[i].host : "-", idle, when);
    }
    if (n == 0)
        fprintf(ctx->out, "No users logged in\n");
    free(u);
    return 0;
}

static int do_show_environment(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_env env;

    if (sys_get_environment(&env) != 0) {
        fprintf(ctx->out, "%% Unable to read power and thermal state\n");
        return 1;
    }
    fprintf(ctx->out, "%-16s %s\n", "Power source:",
            !env.has_battery || env.on_ac > 0 ? "AC power" : (env.on_ac == 0 ? "battery" : "unknown"));

    if (!env.has_battery) {
        fprintf(ctx->out, "%-16s %s\n", "Battery:", "not present");
    } else {
        if (env.percent >= 0)
            fprintf(ctx->out, "%-16s %d%% (%s)\n", "Battery:", env.percent, env.state[0] ? env.state : "unknown");
        if (env.cycles >= 0)
            fprintf(ctx->out, "%-16s %d\n", "Cycle count:", env.cycles);
        if (env.health_pct >= 0)
            fprintf(ctx->out, "%-16s %d%% of design capacity\n", "Health:", env.health_pct);
        if (env.voltage_v >= 0)
            fprintf(ctx->out, "%-16s %.2f V\n", "Voltage:", env.voltage_v);
        if (env.temp_c > -200)
            fprintf(ctx->out, "%-16s %.1f C\n", "Temperature:", env.temp_c);
    }

    if (env.nsensors == 0) {
        fprintf(ctx->out, "%-16s %s\n", "Sensors:", "not available on this host");
        return 0;
    }
    fprintf(ctx->out, "\n%-40s %s\n", "Sensor", "Reading");
    for (int i = 0; i < env.nsensors; i++)
        fprintf(ctx->out, "%-40s %.1f %s\n", env.sensors[i].name, env.sensors[i].value, env.sensors[i].unit);
    return 0;
}

static int do_show_logging(cli_ctx *ctx, int argc, const char *const *argv)
{
    int want = LOG_DEFAULT_LINES;
    char **lines = NULL, err[160];
    int n = 0;

    if (argc > 2) {
        char *end;
        long v = strtol(argv[2], &end, 10);
        if (*argv[2] == '\0' || *end != '\0' || v < 1 || v > LOG_MAX_LINES) {
            fprintf(ctx->out, "%% Invalid count '%s' (use 1-%d)\n", argv[2], LOG_MAX_LINES);
            return 1;
        }
        want = (int)v;
    }
    if (sys_get_log(want, &lines, &n, err, sizeof err) != 0) {
        fprintf(ctx->out, "%% %s\n", err);
        return 1;
    }
    for (int i = 0; i < n; i++)
        fprintf(ctx->out, "%s\n", lines[i]);
    if (n == 0)
        fprintf(ctx->out, "No log messages\n");
    sys_free_lines(lines, n);
    return 0;
}

static int do_show_history(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    for (int i = 0; i < hist_count(); i++)
        fprintf(ctx->out, "%4d  %s\n", i + 1, hist_get(i));
    return 0;
}

void cmds_register_show_system(void)
{
    cmd_register(M_SHOW, "show users", "Users logged in to this machine", do_show_users);
    cmd_register(M_SHOW, "show environment", "Power and thermal status", do_show_environment);
    cmd_register(M_SHOW, "show logging", "Recent system log messages", do_show_logging);
    cmd_register(M_SHOW, "show logging <count>", "Number of lines (1-1000)", do_show_logging);
    cmd_register(M_SHOW, "show history", "Commands entered in this session", do_show_history);
}
