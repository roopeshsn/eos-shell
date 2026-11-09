#include <arpa/inet.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>

#include "cmds.h"
#include "cmdtree.h"
#include "sys/sys.h"

static void print_route_line(cli_ctx *ctx, const sys_route *r)
{
    if (r->gateway[0])
        fprintf(ctx->out, " S        %s/%d via %s, %s\n", r->dest, r->prefix, r->gateway, r->iface);
    else
        fprintf(ctx->out, " C        %s/%d is directly connected, %s\n", r->dest, r->prefix, r->iface);
}

static int print_routes(cli_ctx *ctx, int family)
{
    sys_route *rt;
    int n, have_default = 0;

    if (sys_get_routes(&rt, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the routing table\n");
        return 1;
    }
    fprintf(ctx->out, "Codes: C - connected, S - static (via gateway)\n\n");

    for (int i = 0; i < n; i++) {
        if (rt[i].family != family || rt[i].prefix != 0 || !rt[i].gateway[0])
            continue;
        if (!have_default++)
            fprintf(ctx->out, "Gateway of last resort:\n");
        fprintf(ctx->out, " S        %s/0 via %s, %s\n", rt[i].dest, rt[i].gateway, rt[i].iface);
    }
    if (!have_default)
        fprintf(ctx->out, "Gateway of last resort is not set\n");
    fputc('\n', ctx->out);

    for (int i = 0; i < n; i++) {
        if (rt[i].family == family)
            print_route_line(ctx, &rt[i]);
    }
    free(rt);
    return 0;
}

static int prefix_matches(const unsigned char *addr, const unsigned char *net, int prefix)
{
    int full = prefix / 8, rem = prefix % 8;

    if (memcmp(addr, net, (size_t)full) != 0)
        return 0;
    if (rem == 0)
        return 1;
    unsigned char mask = (unsigned char)(0xff << (8 - rem));
    return (addr[full] & mask) == (net[full] & mask);
}

/* Longest-prefix match; on equal prefixes the lowest metric wins. */
static int lookup_route(cli_ctx *ctx, int family, const char *text)
{
    unsigned char addr[16], net[16];
    sys_route *rt;
    int n, best = -1;

    if (inet_pton(family, text, addr) != 1) {
        fprintf(ctx->out, "%% Invalid address '%s'\n", text);
        return 1;
    }
    if (sys_get_routes(&rt, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the routing table\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (rt[i].family != family || inet_pton(family, rt[i].dest, net) != 1)
            continue;
        if (!prefix_matches(addr, net, rt[i].prefix))
            continue;
        if (best < 0 || rt[i].prefix > rt[best].prefix ||
            (rt[i].prefix == rt[best].prefix && rt[i].metric < rt[best].metric))
            best = i;
    }
    if (best < 0) {
        fprintf(ctx->out, "%% Network not in table\n");
    } else {
        fprintf(ctx->out, "Codes: C - connected, S - static (via gateway)\n\n");
        print_route_line(ctx, &rt[best]);
    }
    free(rt);
    return best < 0;
}

static int do_show_ip_route(cli_ctx *ctx, int argc, const char *const *argv)
{
    return argc > 3 ? lookup_route(ctx, AF_INET, argv[3]) : print_routes(ctx, AF_INET);
}

static int do_show_ipv6_route(cli_ctx *ctx, int argc, const char *const *argv)
{
    return argc > 3 ? lookup_route(ctx, AF_INET6, argv[3]) : print_routes(ctx, AF_INET6);
}

static int route_summary(cli_ctx *ctx, int family)
{
    sys_route *rt;
    int n, connected = 0, via_gw = 0, per_len[129] = {0};

    if (sys_get_routes(&rt, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the routing table\n");
        return 1;
    }
    for (int i = 0; i < n; i++) {
        if (rt[i].family != family || rt[i].prefix < 0 || rt[i].prefix > 128)
            continue;
        if (rt[i].gateway[0])
            via_gw++;
        else
            connected++;
        per_len[rt[i].prefix]++;
    }
    free(rt);

    fprintf(ctx->out, "%-20s %s\n", "Route Source", "Number Of Routes");
    fprintf(ctx->out, "%-20s %s\n", "--------------------", "----------------");
    fprintf(ctx->out, "%-20s %d\n", "connected", connected);
    fprintf(ctx->out, "%-20s %d\n", "static", via_gw);
    fprintf(ctx->out, "%-20s %d\n\n", "Total", connected + via_gw);

    fprintf(ctx->out, "Number of routes per mask-length:\n");
    for (int len = 0; len <= 128; len++) {
        if (per_len[len])
            fprintf(ctx->out, "  /%-4d %d\n", len, per_len[len]);
    }
    return 0;
}

static int do_show_ip_route_summary(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    return route_summary(ctx, AF_INET);
}

static int do_show_ipv6_route_summary(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    return route_summary(ctx, AF_INET6);
}

static int do_show_arp(cli_ctx *ctx, int argc, const char *const *argv)
{
    (void)argc;
    (void)argv;
    sys_arp *arp;
    int n;

    if (sys_get_arp(&arp, &n) != 0) {
        fprintf(ctx->out, "%% Unable to read the ARP table\n");
        return 1;
    }
    fprintf(ctx->out, "%-18s %-16s %s\n", "Address", "Hardware Addr", "Interface");
    for (int i = 0; i < n; i++)
        fprintf(ctx->out, "%-18s %-16s %s\n", arp[i].ip, arp[i].mac, arp[i].iface);
    free(arp);
    return 0;
}

void cmds_register_show_routing(void)
{
    cmd_register(M_SHOW, "show ip route", "IPv4 routing table", do_show_ip_route);
    cmd_register(M_SHOW, "show ip route <addr>", "Route used to reach an IPv4 address", do_show_ip_route);
    cmd_register(M_SHOW, "show ip route summary", "Route counts by source and mask length", do_show_ip_route_summary);
    cmd_register(M_SHOW, "show ip arp", "ARP table", do_show_arp);
    cmd_register(M_SHOW, "show ipv6", "IPv6 information", NULL);
    cmd_register(M_SHOW, "show ipv6 route", "IPv6 routing table", do_show_ipv6_route);
    cmd_register(M_SHOW, "show ipv6 route <addr>", "Route used to reach an IPv6 address", do_show_ipv6_route);
    cmd_register(M_SHOW, "show ipv6 route summary", "Route counts by source and prefix length",
                 do_show_ipv6_route_summary);
    cmd_register(M_SHOW, "show arp", "ARP table", do_show_arp);
}
