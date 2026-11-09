#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/socket.h>

#include "cmds.h"
#include "cmdtree.h"
#include "sys/sys.h"

static const char *link_state(const sys_iface *i)
{
    if (!i->admin_up)
        return "disabled";
    return i->running ? "connected" : "notconnect";
}

static void print_iface(FILE *out, const sys_iface *i)
{
    fprintf(out, "%s is %s, line protocol is %s (%s)\n", i->name,
            !i->admin_up ? "administratively down" : (i->running ? "up" : "down"),
            i->admin_up && i->running ? "up" : "down", link_state(i));
    fprintf(out, "  Hardware is %s", i->loopback ? "Loopback" : "Ethernet");
    if (i->has_mac)
        fprintf(out, ", address is %s", i->mac);
    fputc('\n', out);

    for (int a = 0; a < i->naddr; a++) {
        const sys_addr *ad = &i->addrs[a];
        fprintf(out, "  %s address is %s/%d\n", ad->family == AF_INET ? "Internet" : "IPv6", ad->addr, ad->prefix);
    }
    fprintf(out, "  MTU %u bytes\n", i->mtu);
    fprintf(out, "  %llu packets input, %llu bytes\n", i->rx_pkts, i->rx_bytes);
    fprintf(out, "  %llu input errors\n", i->rx_errs);
    fprintf(out, "  %llu packets output, %llu bytes\n", i->tx_pkts, i->tx_bytes);
    fprintf(out, "  %llu output errors\n", i->tx_errs);
}

static int do_show_interfaces(cli_ctx *ctx, int argc, const char *const *argv)
{
    sys_iface *list;
    int n, found = 0;

    if (sys_get_interfaces(&list, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read interfaces\n");
        return 1;
    }
    for (int k = 0; k < n; k++) {
        if (argc > 2 && strcasecmp(argv[2], list[k].name) != 0)
            continue;
        if (found++)
            fputc('\n', ctx->out);
        print_iface(ctx->out, &list[k]);
    }
    if (!found)
        fprintf(ctx->out, "%% Interface not found: %s\n", argv[2]);
    free(list);
    return found ? 0 : 1;
}

static int do_show_interfaces_status(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_iface *list;
    int n;

    if (sys_get_interfaces(&list, &n) != 0)
        return 1;
    fprintf(ctx->out, "%-14s %-12s %s\n", "Port", "Status", "MTU");
    for (int k = 0; k < n; k++)
        fprintf(ctx->out, "%-14s %-12s %u\n", list[k].name, link_state(&list[k]), list[k].mtu);
    free(list);
    return 0;
}

static int do_show_interfaces_counters(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_iface *list;
    int n;

    if (sys_get_interfaces(&list, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read interfaces\n");
        return 1;
    }
    fprintf(ctx->out, "%-14s %16s %12s %10s %16s %12s %10s\n", "Port", "InOctets", "InPkts", "InErrors", "OutOctets",
            "OutPkts", "OutErrors");
    for (int k = 0; k < n; k++)
        fprintf(ctx->out, "%-14s %16llu %12llu %10llu %16llu %12llu %10llu\n", list[k].name, list[k].rx_bytes,
                list[k].rx_pkts, list[k].rx_errs, list[k].tx_bytes, list[k].tx_pkts, list[k].tx_errs);
    free(list);
    return 0;
}

static int do_show_ip_int_brief(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_iface *list;
    int n;

    if (sys_get_interfaces(&list, &n) != 0)
        return 1;
    fprintf(ctx->out, "%-14s %-22s %-10s %-10s %s\n", "Interface", "IP Address", "Status", "Protocol", "MTU");
    for (int k = 0; k < n; k++) {
        char addr[64] = "unassigned";
        for (int a = 0; a < list[k].naddr; a++) {
            if (list[k].addrs[a].family == AF_INET) {
                snprintf(addr, sizeof addr, "%s/%d", list[k].addrs[a].addr, list[k].addrs[a].prefix);
                break;
            }
        }
        const char *status = list[k].admin_up ? "up" : "down";
        const char *proto = list[k].admin_up && list[k].running ? "up" : "down";
        fprintf(ctx->out, "%-14s %-22s %-10s %-10s %u\n", list[k].name, addr, status, proto, list[k].mtu);
    }
    free(list);
    return 0;
}

void cmds_register_show_interfaces(void)
{
    cmd_register(M_SHOW, "show interfaces", "Interface status and counters", do_show_interfaces);
    cmd_register(M_SHOW, "show interfaces <name>", "Interface name", do_show_interfaces);
    cmd_register(M_SHOW, "show interfaces status", "Brief link status of all interfaces", do_show_interfaces_status);
    cmd_register(M_SHOW, "show interfaces counters", "Packet, byte and error counters", do_show_interfaces_counters);
    cmd_register(M_SHOW, "show ip", "IP information", NULL);
    cmd_register(M_SHOW, "show ip interface", "IP interface status", NULL);
    cmd_register(M_SHOW, "show ip interface brief", "Brief IP interface summary", do_show_ip_int_brief);
}
