#ifdef __APPLE__

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <libproc.h>
#include <mach/mach.h>
#include <mach/mach_time.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/route.h>
#include <net/if_mib.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/sysctl.h>
#include <sys/time.h>
#include <time.h>

#include "sys.h"

void sys_platform_iface_link(sys_iface *ifc, const struct ifaddrs *ifa)
{
    if (!ifa->ifa_addr || ifa->ifa_addr->sa_family != AF_LINK)
        return;

    const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)(const void *)ifa->ifa_addr;
    if (sdl->sdl_alen == 6) {
        sys_format_mac((const unsigned char *)LLADDR(sdl), ifc->mac);
        ifc->has_mac = 1;
    }

    const struct if_data *d = (const struct if_data *)ifa->ifa_data;
    if (d) {
        ifc->mtu = d->ifi_mtu;
        ifc->rx_pkts = d->ifi_ipackets;
        ifc->rx_bytes = d->ifi_ibytes;
        ifc->rx_errs = d->ifi_ierrors;
        ifc->tx_pkts = d->ifi_opackets;
        ifc->tx_bytes = d->ifi_obytes;
        ifc->tx_errs = d->ifi_oerrors;
    }
}

/* getifaddrs() and NET_RT_IFLIST2 report byte counters truncated to 32 bits; ifmib has the real 64-bit ones. */
void sys_platform_iface_post(sys_iface *arr, int n)
{
    for (int i = 0; i < n; i++) {
        unsigned idx = if_nametoindex(arr[i].name);
        if (!idx)
            continue;
        int mib[6] = {CTL_NET, PF_LINK, NETLINK_GENERIC, IFMIB_IFDATA, (int)idx, IFDATA_GENERAL};
        struct ifmibdata d;
        size_t len = sizeof d;
        if (sysctl(mib, 6, &d, &len, NULL, 0) != 0)
            continue;
        arr[i].rx_pkts = d.ifmd_data.ifi_ipackets;
        arr[i].rx_bytes = d.ifmd_data.ifi_ibytes;
        arr[i].rx_errs = d.ifmd_data.ifi_ierrors;
        arr[i].tx_pkts = d.ifmd_data.ifi_opackets;
        arr[i].tx_bytes = d.ifmd_data.ifi_obytes;
        arr[i].tx_errs = d.ifmd_data.ifi_oerrors;
    }
}

/* ---- routing table and ARP via the PF_ROUTE sysctl ---- */

#define SA_ROUNDUP(len) ((len) > 0 ? (((len) + sizeof(uint32_t) - 1) & ~(sizeof(uint32_t) - 1)) : sizeof(uint32_t))

static char *route_dump(int af, int op, int flags, size_t *len)
{
    int mib[6] = {CTL_NET, PF_ROUTE, 0, af, op, flags};
    *len = 0;
    if (sysctl(mib, 6, NULL, len, NULL, 0) < 0)
        return NULL;
    char *buf = malloc(*len ? *len : 1);
    if (buf && sysctl(mib, 6, buf, len, NULL, 0) < 0) {
        free(buf);
        return NULL;
    }
    return buf;
}

static void split_sockaddrs(const char *p, const char *end, int addrs, const struct sockaddr *sa[RTAX_MAX])
{
    memset(sa, 0, RTAX_MAX * sizeof *sa);
    for (int i = 0; i < RTAX_MAX && p < end; i++) {
        if (!(addrs & (1 << i)))
            continue;
        sa[i] = (const struct sockaddr *)(const void *)p;
        p += SA_ROUNDUP(sa[i]->sa_len);
    }
}

/* Copies up to `want` address bytes that sit at `off` inside a possibly truncated sockaddr. */
static void sa_bytes(const struct sockaddr *sa, size_t off, unsigned char *dst, size_t want)
{
    memset(dst, 0, want);
    if (sa->sa_len > off)
        memcpy(dst, (const char *)sa + off, sa->sa_len - off < want ? sa->sa_len - off : want);
}

static int sa_to_str(const struct sockaddr *sa, char *buf, size_t n)
{
    unsigned char b[16];
    if (sa->sa_family == AF_INET) {
        sa_bytes(sa, 4, b, 4);
        return inet_ntop(AF_INET, b, buf, (socklen_t)n) ? 0 : -1;
    }
    if (sa->sa_family == AF_INET6) {
        sa_bytes(sa, 8, b, 16);
        if (b[0] == 0xfe && (b[1] & 0xc0) == 0x80)
            b[2] = b[3] = 0;
        return inet_ntop(AF_INET6, b, buf, (socklen_t)n) ? 0 : -1;
    }
    return -1;
}

static int mask_prefix(const struct sockaddr *mask, int family)
{
    unsigned char b[16];
    size_t nbytes = family == AF_INET ? 4 : 16;
    int bits = 0;

    sa_bytes(mask, family == AF_INET ? 4 : 8, b, nbytes);
    for (size_t i = 0; i < nbytes; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            if (!(b[i] & (1 << bit)))
                return bits;
            bits++;
        }
    }
    return bits;
}

int sys_get_routes(sys_route **out, int *count)
{
    size_t len;
    char *buf = route_dump(AF_UNSPEC, NET_RT_DUMP, 0, &len);
    int n = 0, cap = 64;
    sys_route *arr = calloc((size_t)cap, sizeof *arr);

    if (!buf || !arr) {
        free(buf);
        free(arr);
        return -1;
    }
    for (const char *p = buf; p < buf + len;) {
        const struct rt_msghdr *rtm = (const struct rt_msghdr *)(const void *)p;
        if (rtm->rtm_msglen == 0)
            break;
        const char *next = p + rtm->rtm_msglen;
        const struct sockaddr *sa[RTAX_MAX];
        split_sockaddrs(p + sizeof *rtm, next, rtm->rtm_addrs, sa);

        int skip = RTF_WASCLONED | RTF_BROADCAST | RTF_IFSCOPE;
        int neighbor = (rtm->rtm_flags & RTF_LLINFO) && sa[RTAX_GATEWAY] && sa[RTAX_GATEWAY]->sa_family == AF_LINK;
        if ((rtm->rtm_flags & RTF_UP) && !(rtm->rtm_flags & skip) && !neighbor && sa[RTAX_DST] &&
            (sa[RTAX_DST]->sa_family == AF_INET || sa[RTAX_DST]->sa_family == AF_INET6)) {
            if (n == cap) {
                cap *= 2;
                sys_route *grown = realloc(arr, (size_t)cap * sizeof *arr);
                if (!grown) {
                    free(arr);
                    free(buf);
                    return -1;
                }
                arr = grown;
            }
            sys_route *r = &arr[n++];
            memset(r, 0, sizeof *r);
            r->family = sa[RTAX_DST]->sa_family;
            sa_to_str(sa[RTAX_DST], r->dest, sizeof r->dest);
            if (rtm->rtm_flags & RTF_HOST)
                r->prefix = r->family == AF_INET ? 32 : 128;
            else if (sa[RTAX_NETMASK])
                r->prefix = mask_prefix(sa[RTAX_NETMASK], r->family);
            if (sa[RTAX_GATEWAY]) /* link-layer gateways (AF_LINK) fail here and stay "connected" */
                sa_to_str(sa[RTAX_GATEWAY], r->gateway, sizeof r->gateway);
            if (strcmp(r->gateway, r->dest) == 0) /* loopback-style host route: the destination is local */
                r->gateway[0] = '\0';
            if (!if_indextoname(rtm->rtm_index, r->iface))
                snprintf(r->iface, sizeof r->iface, "?");
        }
        p = next;
    }
    free(buf);
    sys_sort_routes(arr, n);
    *out = arr;
    *count = n;
    return 0;
}

int sys_get_arp(sys_arp **out, int *count)
{
    size_t len;
    char *buf = route_dump(AF_INET, NET_RT_FLAGS, RTF_LLINFO, &len);
    int n = 0, cap = 32;
    sys_arp *arr = calloc((size_t)cap, sizeof *arr);

    if (!buf || !arr) {
        free(buf);
        free(arr);
        return -1;
    }
    for (const char *p = buf; p < buf + len;) {
        const struct rt_msghdr *rtm = (const struct rt_msghdr *)(const void *)p;
        if (rtm->rtm_msglen == 0)
            break;
        const char *next = p + rtm->rtm_msglen;
        const struct sockaddr *sa[RTAX_MAX];
        split_sockaddrs(p + sizeof *rtm, next, rtm->rtm_addrs, sa);

        if (sa[RTAX_DST] && sa[RTAX_DST]->sa_family == AF_INET && sa[RTAX_GATEWAY] &&
            sa[RTAX_GATEWAY]->sa_family == AF_LINK) {
            const struct sockaddr_dl *sdl = (const struct sockaddr_dl *)(const void *)sa[RTAX_GATEWAY];
            if (sdl->sdl_alen == 6) {
                if (n == cap) {
                    cap *= 2;
                    sys_arp *grown = realloc(arr, (size_t)cap * sizeof *arr);
                    if (!grown) {
                        free(arr);
                        free(buf);
                        return -1;
                    }
                    arr = grown;
                }
                sys_arp *a = &arr[n++];
                memset(a, 0, sizeof *a);
                sa_to_str(sa[RTAX_DST], a->ip, sizeof a->ip);
                sys_format_mac((const unsigned char *)LLADDR(sdl), a->mac);
                if (!if_indextoname(rtm->rtm_index, a->iface))
                    snprintf(a->iface, sizeof a->iface, "?");
            }
        }
        p = next;
    }
    free(buf);
    *out = arr;
    *count = n;
    return 0;
}

/* ---- processes ---- */

static int proc_cmp(const void *x, const void *y)
{
    return ((const sys_proc *)x)->pid - ((const sys_proc *)y)->pid;
}

int sys_get_processes(sys_proc **out, int *count)
{
    int mib[3] = {CTL_KERN, KERN_PROC, KERN_PROC_ALL};
    size_t len = 0;

    /* The process table can grow between the two calls, so retry with some slack. */
    struct kinfo_proc *kp = NULL;
    for (int attempt = 0; attempt < 5; attempt++) {
        if (sysctl(mib, 3, NULL, &len, NULL, 0) < 0)
            return -1;
        len += len / 8 + sizeof *kp * 16;
        free(kp);
        kp = malloc(len);
        if (!kp)
            return -1;
        if (sysctl(mib, 3, kp, &len, NULL, 0) == 0)
            break;
        free(kp);
        kp = NULL;
    }
    if (!kp)
        return -1;

    int total = (int)(len / sizeof *kp), n = 0;
    sys_proc *arr = calloc((size_t)total + 1, sizeof *arr);
    if (!arr) {
        free(kp);
        return -1;
    }

    mach_timebase_info_data_t tb;
    mach_timebase_info(&tb);

    for (int i = 0; i < total; i++) {
        const struct kinfo_proc *k = &kp[i];
        sys_proc *p = &arr[n++];

        p->pid = k->kp_proc.p_pid;
        p->ppid = k->kp_eproc.e_ppid;
        sys_username(k->kp_eproc.e_ucred.cr_uid, p->user, sizeof p->user);
        snprintf(p->name, sizeof p->name, "%s", k->kp_proc.p_comm);
        p->state = k->kp_proc.p_stat == SSTOP ? 'T' : k->kp_proc.p_stat == SZOMB ? 'Z' : 'S';

        /* Richer data (full name, memory, CPU, running state) is only readable for our own processes. */
        struct proc_taskallinfo ti;
        if (proc_pidinfo(p->pid, PROC_PIDTASKALLINFO, 0, &ti, sizeof ti) == (int)sizeof ti) {
            if (ti.pbsd.pbi_name[0])
                snprintf(p->name, sizeof p->name, "%s", ti.pbsd.pbi_name);
            if (p->state == 'S' && ti.ptinfo.pti_numrunning > 0)
                p->state = 'R';
            p->rss = ti.ptinfo.pti_resident_size;
            double ticks = (double)(ti.ptinfo.pti_total_user + ti.ptinfo.pti_total_system);
            p->cpu_secs = ticks * tb.numer / tb.denom / 1e9;
        }
    }
    free(kp);
    qsort(arr, (size_t)n, sizeof *arr, proc_cmp);
    *out = arr;
    *count = n;
    return 0;
}

static int sysctl_str(const char *name, char *buf, size_t n)
{
    size_t len = n;
    if (sysctlbyname(name, buf, &len, NULL, 0) != 0) {
        buf[0] = '\0';
        return -1;
    }
    return 0;
}

/* ---- battery (IORegistry, via ioreg to avoid linking IOKit) ---- */

int sys_get_environment(sys_env *env)
{
    char **lines = NULL;
    int n = 0;
    long long design = -1, raw_max = -1;
    int charging = 0, full = 0;

    memset(env, 0, sizeof *env);
    env->percent = env->on_ac = env->cycles = env->health_pct = -1;
    env->voltage_v = -1;
    env->temp_c = -1000;

    if (sys_run_lines("/usr/sbin/ioreg -rn AppleSmartBattery -w0 2>/dev/null", 4000, &lines, &n) != 0)
        return 0; /* no battery information: desktop Mac or ioreg unavailable */

    for (int i = 0; i < n; i++) {
        char key[64], val[64];
        /* Top-level properties are indented six spaces; the nested BatteryData dictionary is not. */
        if (strncmp(lines[i], "      \"", 7) != 0 ||
            sscanf(lines[i], " \"%63[^\"]\" = %63s", key, val) != 2)
            continue;
        if (strcmp(key, "BatteryInstalled") == 0)
            env->has_battery = strcmp(val, "Yes") == 0;
        else if (strcmp(key, "CurrentCapacity") == 0)
            env->percent = atoi(val);
        else if (strcmp(key, "ExternalConnected") == 0)
            env->on_ac = strcmp(val, "Yes") == 0;
        else if (strcmp(key, "IsCharging") == 0)
            charging = strcmp(val, "Yes") == 0;
        else if (strcmp(key, "FullyCharged") == 0)
            full = strcmp(val, "Yes") == 0;
        else if (strcmp(key, "CycleCount") == 0)
            env->cycles = atoi(val);
        else if (strcmp(key, "Voltage") == 0)
            env->voltage_v = atof(val) / 1000.0;
        else if (strcmp(key, "Temperature") == 0)
            env->temp_c = atof(val) / 100.0;
        else if (strcmp(key, "DesignCapacity") == 0)
            design = atoll(val);
        else if (strcmp(key, "AppleRawMaxCapacity") == 0)
            raw_max = atoll(val);
    }
    sys_free_lines(lines, n);

    if (design > 0 && raw_max > 0)
        env->health_pct = (int)(raw_max * 100 / design);
    snprintf(env->state, sizeof env->state, "%s",
             charging ? "charging" : full ? "charged" : env->on_ac > 0 ? "not charging" : "discharging");
    return 0;
}

/* ---- system log ---- */

int sys_get_log(int max_lines, char ***lines, int *count, char *err, size_t errlen)
{
    /* The unified log needs an admin user; older systems and some setups still have a text file. */
    if (sys_run_lines("/usr/bin/log show --last 10m --style syslog 2>/dev/null", max_lines, lines, count) == 0)
        return 0;
    if (sys_tail_file("/var/log/system.log", max_lines, lines, count) == 0)
        return 0;
    snprintf(err, errlen, "Unable to read the system log (the unified log needs an admin user)");
    return -1;
}

void sys_platform_info(sys_info *info)
{
    char ver[32], build[32];

    sysctl_str("hw.model", info->model, sizeof info->model);
    if (sysctl_str("kern.osproductversion", ver, sizeof ver) == 0) {
        if (sysctl_str("kern.osversion", build, sizeof build) == 0)
            snprintf(info->os, sizeof info->os, "macOS %s (%s)", ver, build);
        else
            snprintf(info->os, sizeof info->os, "macOS %s", ver);
    }

    uint64_t memsize = 0;
    size_t len = sizeof memsize;
    if (sysctlbyname("hw.memsize", &memsize, &len, NULL, 0) == 0)
        info->mem_total = memsize;

    struct timeval boot;
    len = sizeof boot;
    if (sysctlbyname("kern.boottime", &boot, &len, NULL, 0) == 0)
        info->uptime_secs = (unsigned long long)(time(NULL) - boot.tv_sec);

    vm_statistics64_data_t vm;
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    host_page_size(mach_host_self(), &page);
    if (host_statistics64(mach_host_self(), HOST_VM_INFO64, (host_info64_t)&vm, &count) == KERN_SUCCESS)
        info->mem_free = ((unsigned long long)vm.free_count + vm.inactive_count + vm.speculative_count) * page;
}

#endif
