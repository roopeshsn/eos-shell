#ifdef __linux__

#include <arpa/inet.h>
#include <dirent.h>
#include <ifaddrs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

#include "sys.h"

static int read_file(const char *path, char *buf, size_t n)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    if (!fgets(buf, (int)n, f)) {
        fclose(f);
        return -1;
    }
    fclose(f);
    buf[strcspn(buf, "\r\n")] = '\0';
    return 0;
}

static unsigned long long read_u64(const char *name, const char *file)
{
    char path[160], buf[64];
    snprintf(path, sizeof path, "/sys/class/net/%s/%s", name, file);
    return read_file(path, buf, sizeof buf) == 0 ? strtoull(buf, NULL, 10) : 0;
}

void sys_platform_iface_link(sys_iface *ifc, const struct ifaddrs *ifa)
{
    (void)ifa;
    if (ifc->mtu)
        return; /* sysfs already read for this interface */

    char path[160], buf[64];
    ifc->mtu = (unsigned)read_u64(ifc->name, "mtu");

    snprintf(path, sizeof path, "/sys/class/net/%s/address", ifc->name);
    unsigned m[6];
    if (read_file(path, buf, sizeof buf) == 0 &&
        sscanf(buf, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) == 6 &&
        (m[0] | m[1] | m[2] | m[3] | m[4] | m[5]) != 0) {
        unsigned char mac[6];
        for (int i = 0; i < 6; i++)
            mac[i] = (unsigned char)m[i];
        sys_format_mac(mac, ifc->mac);
        ifc->has_mac = 1;
    }

    ifc->rx_pkts = read_u64(ifc->name, "statistics/rx_packets");
    ifc->rx_bytes = read_u64(ifc->name, "statistics/rx_bytes");
    ifc->rx_errs = read_u64(ifc->name, "statistics/rx_errors");
    ifc->tx_pkts = read_u64(ifc->name, "statistics/tx_packets");
    ifc->tx_bytes = read_u64(ifc->name, "statistics/tx_bytes");
    ifc->tx_errs = read_u64(ifc->name, "statistics/tx_errors");
}

void sys_platform_iface_post(sys_iface *arr, int n)
{
    (void)arr;
    (void)n;
}

/* ---- routes and ARP from /proc/net ---- */

static int route_push(sys_route **arr, int *n, int *cap, const sys_route *r)
{
    if (*n == *cap) {
        *cap *= 2;
        sys_route *grown = realloc(*arr, (size_t)*cap * sizeof **arr);
        if (!grown)
            return -1;
        *arr = grown;
    }
    (*arr)[(*n)++] = *r;
    return 0;
}

static int prefix_from_mask(unsigned mask_host_order)
{
    int bits = 0;
    while (bits < 32 && (mask_host_order & (0x80000000u >> bits)))
        bits++;
    return bits;
}

int sys_get_routes(sys_route **out, int *count)
{
    int n = 0, cap = 32;
    sys_route *arr = calloc((size_t)cap, sizeof *arr);
    char line[512];

    if (!arr)
        return -1;

    /* IPv4: Iface Destination Gateway Flags RefCnt Use Metric Mask ... (little-endian hex) */
    FILE *f = fopen("/proc/net/route", "r");
    if (f) {
        if (fgets(line, sizeof line, f)) { /* header */
            char iface[32];
            unsigned dest, gw, flags, refcnt, use, metric, mask;
            while (fgets(line, sizeof line, f)) {
                if (sscanf(line, "%31s %x %x %x %u %u %u %x", iface, &dest, &gw, &flags, &refcnt, &use, &metric,
                           &mask) != 8 || !(flags & 0x1))
                    continue;
                sys_route r;
                memset(&r, 0, sizeof r);
                r.family = AF_INET;
                inet_ntop(AF_INET, &dest, r.dest, sizeof r.dest);
                r.prefix = prefix_from_mask(ntohl(mask));
                if (flags & 0x2)
                    inet_ntop(AF_INET, &gw, r.gateway, sizeof r.gateway);
                snprintf(r.iface, sizeof r.iface, "%s", iface);
                r.metric = (int)metric;
                if (route_push(&arr, &n, &cap, &r) != 0) {
                    fclose(f);
                    free(arr);
                    return -1;
                }
            }
        }
        fclose(f);
    }

    /* IPv6: dest(32 hex) plen src splen nexthop metric refcnt use flags iface */
    f = fopen("/proc/net/ipv6_route", "r");
    if (f) {
        while (fgets(line, sizeof line, f)) {
            char dest[33], src[33], nh[33], iface[32];
            unsigned plen, splen, metric, refcnt, use, flags;
            if (sscanf(line, "%32s %x %32s %x %32s %x %x %x %x %31s", dest, &plen, src, &splen, nh, &metric, &refcnt,
                       &use, &flags, iface) != 10 || !(flags & 0x1))
                continue;
            if (strcmp(iface, "lo") == 0 && plen == 0)
                continue; /* kernel's unreachable default on lo */

            unsigned char d[16], g[16];
            for (int i = 0; i < 16; i++) {
                unsigned x, y;
                sscanf(dest + 2 * i, "%2x", &x);
                sscanf(nh + 2 * i, "%2x", &y);
                d[i] = (unsigned char)x;
                g[i] = (unsigned char)y;
            }
            sys_route r;
            memset(&r, 0, sizeof r);
            r.family = AF_INET6;
            inet_ntop(AF_INET6, d, r.dest, sizeof r.dest);
            r.prefix = (int)plen;
            if (flags & 0x2)
                inet_ntop(AF_INET6, g, r.gateway, sizeof r.gateway);
            snprintf(r.iface, sizeof r.iface, "%s", iface);
            r.metric = (int)metric;
            if (route_push(&arr, &n, &cap, &r) != 0) {
                fclose(f);
                free(arr);
                return -1;
            }
        }
        fclose(f);
    }

    sys_sort_routes(arr, n);
    *out = arr;
    *count = n;
    return 0;
}

int sys_get_arp(sys_arp **out, int *count)
{
    int n = 0, cap = 32;
    sys_arp *arr = calloc((size_t)cap, sizeof *arr);
    char line[256];

    if (!arr)
        return -1;
    FILE *f = fopen("/proc/net/arp", "r");
    if (f) {
        if (fgets(line, sizeof line, f)) { /* header */
            char ip[48], mac[32], mask[32], dev[32];
            unsigned hw, flags;
            while (fgets(line, sizeof line, f)) {
                unsigned m[6];
                if (sscanf(line, "%47s 0x%x 0x%x %31s %31s %31s", ip, &hw, &flags, mac, mask, dev) != 6 ||
                    flags == 0 ||
                    sscanf(mac, "%x:%x:%x:%x:%x:%x", &m[0], &m[1], &m[2], &m[3], &m[4], &m[5]) != 6)
                    continue;
                if (n == cap) {
                    cap *= 2;
                    sys_arp *grown = realloc(arr, (size_t)cap * sizeof *arr);
                    if (!grown) {
                        fclose(f);
                        free(arr);
                        return -1;
                    }
                    arr = grown;
                }
                unsigned char b[6];
                for (int i = 0; i < 6; i++)
                    b[i] = (unsigned char)m[i];
                sys_arp *a = &arr[n++];
                memset(a, 0, sizeof *a);
                snprintf(a->ip, sizeof a->ip, "%s", ip);
                sys_format_mac(b, a->mac);
                snprintf(a->iface, sizeof a->iface, "%s", dev);
            }
        }
        fclose(f);
    }
    *out = arr;
    *count = n;
    return 0;
}

/* ---- processes from /proc ---- */

static int proc_cmp(const void *x, const void *y)
{
    return ((const sys_proc *)x)->pid - ((const sys_proc *)y)->pid;
}

int sys_get_processes(sys_proc **out, int *count)
{
    DIR *d = opendir("/proc");
    int n = 0, cap = 256;
    sys_proc *arr = calloc((size_t)cap, sizeof *arr);
    long ticks = sysconf(_SC_CLK_TCK), page = sysconf(_SC_PAGESIZE);

    if (!d || !arr) {
        if (d)
            closedir(d);
        free(arr);
        return -1;
    }
    const struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        if (de->d_name[0] < '1' || de->d_name[0] > '9')
            continue;

        char path[300], buf[1024];
        snprintf(path, sizeof path, "/proc/%s/stat", de->d_name);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        size_t len = fread(buf, 1, sizeof buf - 1, f);
        fclose(f);
        buf[len] = '\0';

        /* "pid (comm) state ppid ..." where comm may itself contain spaces and parentheses. */
        char *open = strchr(buf, '(');
        char *close = strrchr(buf, ')');
        if (!open || !close || close < open)
            continue;

        char state;
        int ppid;
        unsigned long utime, stime;
        long rss;
        if (sscanf(close + 1, " %c %d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu %*d %*d %*d %*d %*d %*d %*u %*u %ld",
                   &state, &ppid, &utime, &stime, &rss) != 5)
            continue;

        if (n == cap) {
            cap *= 2;
            sys_proc *grown = realloc(arr, (size_t)cap * sizeof *arr);
            if (!grown) {
                closedir(d);
                free(arr);
                return -1;
            }
            arr = grown;
        }
        sys_proc *p = &arr[n++];
        memset(p, 0, sizeof *p);
        p->pid = atoi(de->d_name);
        p->ppid = ppid;
        p->state = state;
        p->rss = rss > 0 ? (unsigned long long)rss * (unsigned long long)page : 0;
        p->cpu_secs = (double)(utime + stime) / (double)ticks;
        *close = '\0';
        snprintf(p->name, sizeof p->name, "%s", open + 1);

        struct stat st;
        snprintf(path, sizeof path, "/proc/%s", de->d_name);
        if (stat(path, &st) == 0)
            sys_username(st.st_uid, p->user, sizeof p->user);
        else
            snprintf(p->user, sizeof p->user, "?");
    }
    closedir(d);
    qsort(arr, (size_t)n, sizeof *arr, proc_cmp);
    *out = arr;
    *count = n;
    return 0;
}

static void os_release(char *out, size_t n)
{
    FILE *f = fopen("/etc/os-release", "r");
    char line[256];

    if (!f)
        return;
    while (fgets(line, sizeof line, f)) {
        if (strncmp(line, "PRETTY_NAME=", 12) == 0) {
            char *v = line + 12;
            v[strcspn(v, "\r\n")] = '\0';
            size_t len = strlen(v);
            if (len >= 2 && v[0] == '"' && v[len - 1] == '"') {
                v[len - 1] = '\0';
                v++;
            }
            size_t keep = strlen(v) < n ? strlen(v) : n - 1;
            memcpy(out, v, keep);
            out[keep] = '\0';
            break;
        }
    }
    fclose(f);
}

/* ---- power and thermal sensors ---- */

static int read_ll(const char *path, long long *v)
{
    char buf[64];
    if (read_file(path, buf, sizeof buf) != 0)
        return -1;
    *v = atoll(buf);
    return 0;
}

static void add_sensor(sys_env *env, const char *name, double value, const char *unit)
{
    if (env->nsensors >= SYS_MAX_SENSORS)
        return;
    sys_sensor *s = &env->sensors[env->nsensors++];
    snprintf(s->name, sizeof s->name, "%.55s", name);
    s->value = value;
    snprintf(s->unit, sizeof s->unit, "%s", unit);
}

static void scan_power_supply(sys_env *env)
{
    DIR *d = opendir("/sys/class/power_supply");
    const struct dirent *de;

    if (!d)
        return;
    while ((de = readdir(d)) != NULL) {
        char dir[300], path[400], type[32], status[32];
        long long v, full = -1, design = -1;

        if (de->d_name[0] == '.')
            continue;
        snprintf(dir, sizeof dir, "/sys/class/power_supply/%.200s", de->d_name);
        snprintf(path, sizeof path, "%s/type", dir);
        if (read_file(path, type, sizeof type) != 0)
            continue;

        if (strcmp(type, "Mains") == 0) {
            snprintf(path, sizeof path, "%s/online", dir);
            if (read_ll(path, &v) == 0 && v > 0)
                env->on_ac = 1;
            else if (env->on_ac < 0)
                env->on_ac = 0;
            continue;
        }
        if (strcmp(type, "Battery") != 0 || env->has_battery)
            continue;

        env->has_battery = 1;
        snprintf(path, sizeof path, "%s/capacity", dir);
        if (read_ll(path, &v) == 0)
            env->percent = (int)v;
        snprintf(path, sizeof path, "%s/status", dir);
        if (read_file(path, status, sizeof status) == 0) {
            for (char *c = status; *c; c++)
                *c = (char)((*c >= 'A' && *c <= 'Z') ? *c + 32 : *c);
            snprintf(env->state, sizeof env->state, "%.23s", strcmp(status, "full") == 0 ? "charged" : status);
        }
        snprintf(path, sizeof path, "%s/cycle_count", dir);
        if (read_ll(path, &v) == 0 && v >= 0)
            env->cycles = (int)v;
        snprintf(path, sizeof path, "%s/voltage_now", dir);
        if (read_ll(path, &v) == 0)
            env->voltage_v = (double)v / 1e6;
        snprintf(path, sizeof path, "%s/temp", dir);
        if (read_ll(path, &v) == 0)
            env->temp_c = (double)v / 10.0;

        snprintf(path, sizeof path, "%s/energy_full", dir);
        if (read_ll(path, &full) != 0) {
            snprintf(path, sizeof path, "%s/charge_full", dir);
            read_ll(path, &full);
        }
        snprintf(path, sizeof path, "%s/energy_full_design", dir);
        if (read_ll(path, &design) != 0) {
            snprintf(path, sizeof path, "%s/charge_full_design", dir);
            read_ll(path, &design);
        }
        if (full > 0 && design > 0)
            env->health_pct = (int)(full * 100 / design);
    }
    closedir(d);
}

static void scan_thermal_zones(sys_env *env)
{
    DIR *d = opendir("/sys/class/thermal");
    const struct dirent *de;

    if (!d)
        return;
    while ((de = readdir(d)) != NULL) {
        char path[400], name[48];
        long long milli;

        if (strncmp(de->d_name, "thermal_zone", 12) != 0)
            continue;
        snprintf(path, sizeof path, "/sys/class/thermal/%.200s/type", de->d_name);
        if (read_file(path, name, sizeof name) != 0)
            snprintf(name, sizeof name, "%.40s", de->d_name);
        snprintf(path, sizeof path, "/sys/class/thermal/%.200s/temp", de->d_name);
        if (read_ll(path, &milli) == 0)
            add_sensor(env, name, (double)milli / 1000.0, "C");
    }
    closedir(d);
}

static void scan_hwmon(sys_env *env)
{
    for (int h = 0; h < 16; h++) {
        char dir[64], path[128], chip[32], label[32], name[56];
        long long v;

        snprintf(dir, sizeof dir, "/sys/class/hwmon/hwmon%d", h);
        snprintf(path, sizeof path, "%s/name", dir);
        if (read_file(path, chip, sizeof chip) != 0)
            continue;
        for (int i = 1; i <= 16; i++) {
            snprintf(path, sizeof path, "%s/temp%d_input", dir, i);
            if (read_ll(path, &v) == 0) {
                snprintf(path, sizeof path, "%s/temp%d_label", dir, i);
                if (read_file(path, label, sizeof label) != 0)
                    snprintf(label, sizeof label, "temp%d", i);
                snprintf(name, sizeof name, "%.20s %.30s", chip, label);
                add_sensor(env, name, (double)v / 1000.0, "C");
            }
            snprintf(path, sizeof path, "%s/fan%d_input", dir, i);
            if (read_ll(path, &v) == 0) {
                snprintf(name, sizeof name, "%.20s fan%d", chip, i);
                add_sensor(env, name, (double)v, "RPM");
            }
        }
    }
}

int sys_get_environment(sys_env *env)
{
    memset(env, 0, sizeof *env);
    env->percent = env->on_ac = env->cycles = env->health_pct = -1;
    env->voltage_v = -1;
    env->temp_c = -1000;

    scan_power_supply(env);
    scan_thermal_zones(env);
    scan_hwmon(env);
    return 0;
}

/* ---- system log ---- */

int sys_get_log(int max_lines, char ***lines, int *count, char *err, size_t errlen)
{
    char cmd[96];

    snprintf(cmd, sizeof cmd, "journalctl -n %d --no-pager -q 2>/dev/null", max_lines);
    if (sys_run_lines(cmd, max_lines, lines, count) == 0)
        return 0;
    if (sys_tail_file("/var/log/syslog", max_lines, lines, count) == 0)
        return 0;
    if (sys_tail_file("/var/log/messages", max_lines, lines, count) == 0)
        return 0;
    snprintf(err, errlen, "No readable system log (journalctl, /var/log/syslog and /var/log/messages unavailable)");
    return -1;
}

void sys_platform_info(sys_info *info)
{
    char buf[128];

    os_release(info->os, sizeof info->os);
    if (info->os[0] == '\0')
        snprintf(info->os, sizeof info->os, "Linux");

    if (read_file("/sys/class/dmi/id/product_name", info->model, sizeof info->model) != 0)
        read_file("/proc/device-tree/model", info->model, sizeof info->model);
    read_file("/sys/class/dmi/id/product_serial", info->serial, sizeof info->serial);

    if (read_file("/proc/uptime", buf, sizeof buf) == 0)
        info->uptime_secs = (unsigned long long)strtod(buf, NULL);

    FILE *f = fopen("/proc/meminfo", "r");
    if (f) {
        char line[128];
        unsigned long long kb;
        while (fgets(line, sizeof line, f)) {
            if (sscanf(line, "MemTotal: %llu kB", &kb) == 1)
                info->mem_total = kb * 1024;
            else if (sscanf(line, "MemAvailable: %llu kB", &kb) == 1)
                info->mem_free = kb * 1024;
        }
        fclose(f);
    }
}

#endif
