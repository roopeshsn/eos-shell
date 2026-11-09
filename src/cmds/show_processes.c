#include <stdlib.h>

#include "cmds.h"
#include "cmdtree.h"
#include "sys/sys.h"

#define TOP_COUNT 15

static int by_rss_desc(const void *x, const void *y)
{
    const sys_proc *a = x, *b = y;
    if (a->rss != b->rss)
        return a->rss < b->rss ? 1 : -1;
    return a->pid - b->pid;
}

static void print_procs(FILE *out, const sys_proc *p, int n)
{
    fprintf(out, "%7s %7s %-12s %-5s %9s %10s  %s\n", "PID", "PPID", "User", "State", "RSS(MB)", "CPU(s)", "Command");
    for (int i = 0; i < n; i++)
        fprintf(out, "%7d %7d %-12.12s %-5c %9.1f %10.1f  %s\n", p[i].pid, p[i].ppid, p[i].user, p[i].state,
                (double)p[i].rss / (1 << 20), p[i].cpu_secs, p[i].name);
}

static int do_show_processes(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_proc *p;
    int n;

    if (sys_get_processes(&p, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the process list\n");
        return 1;
    }
    print_procs(ctx->out, p, n);
    free(p);
    return 0;
}

static int do_show_processes_top(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_proc *p;
    int n;

    if (sys_get_processes(&p, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the process list\n");
        return 1;
    }
    qsort(p, (size_t)n, sizeof *p, by_rss_desc);
    print_procs(ctx->out, p, n < TOP_COUNT ? n : TOP_COUNT);
    free(p);
    return 0;
}

void cmds_register_show_processes(void)
{
    cmd_register(M_SHOW, "show processes", "Running processes", do_show_processes);
    cmd_register(M_SHOW, "show processes top", "Top processes by memory", do_show_processes_top);
}
