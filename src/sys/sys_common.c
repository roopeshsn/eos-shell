#include "sys.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/utsname.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <utmpx.h>

void sys_format_mac(const unsigned char mac[6], char out[20])
{
    snprintf(out, 20, "%02x%02x.%02x%02x.%02x%02x", mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
}

void sys_format_uptime(unsigned long long secs, char *buf, size_t n)
{
    unsigned long long d = secs / 86400, h = secs % 86400 / 3600, m = secs % 3600 / 60;
    if (d)
        snprintf(buf, n, "%llu day%s, %llu hour%s, %llu minute%s", d, d == 1 ? "" : "s", h,
                 h == 1 ? "" : "s", m, m == 1 ? "" : "s");
    else if (h)
        snprintf(buf, n, "%llu hour%s, %llu minute%s", h, h == 1 ? "" : "s", m, m == 1 ? "" : "s");
    else
        snprintf(buf, n, "%llu minute%s", m, m == 1 ? "" : "s");
}

static int prefix_len(const unsigned char *mask, int nbytes)
{
    int bits = 0;
    for (int i = 0; i < nbytes; i++) {
        for (int b = 7; b >= 0; b--) {
            if (!(mask[i] & (1 << b)))
                return bits;
            bits++;
        }
    }
    return bits;
}

static void add_addr(sys_iface *ifc, const struct ifaddrs *ifa)
{
    if (ifc->naddr >= SYS_MAX_ADDRS || !ifa->ifa_addr)
        return;
    sys_addr *a = &ifc->addrs[ifc->naddr];
    a->family = ifa->ifa_addr->sa_family;

    if (a->family == AF_INET) {
        const struct sockaddr_in *sin = (const struct sockaddr_in *)(const void *)ifa->ifa_addr;
        inet_ntop(AF_INET, &sin->sin_addr, a->addr, sizeof a->addr);
        if (ifa->ifa_netmask) {
            const struct sockaddr_in *m = (const struct sockaddr_in *)(const void *)ifa->ifa_netmask;
            a->prefix = prefix_len((const unsigned char *)&m->sin_addr, 4);
        }
    } else if (a->family == AF_INET6) {
        struct in6_addr addr = ((const struct sockaddr_in6 *)(const void *)ifa->ifa_addr)->sin6_addr;
        /* BSD kernels embed the scope id in bytes 2-3 of link-local addresses. */
        if (addr.s6_addr[0] == 0xfe && (addr.s6_addr[1] & 0xc0) == 0x80)
            addr.s6_addr[2] = addr.s6_addr[3] = 0;
        inet_ntop(AF_INET6, &addr, a->addr, sizeof a->addr);
        if (ifa->ifa_netmask) {
            const struct sockaddr_in6 *m = (const struct sockaddr_in6 *)(const void *)ifa->ifa_netmask;
            a->prefix = prefix_len(m->sin6_addr.s6_addr, 16);
        }
    } else {
        return;
    }
    ifc->naddr++;
}

/* "en2" sorts before "en10". */
static int natural_cmp(const char *a, const char *b)
{
    while (*a && *b) {
        if (*a >= '0' && *a <= '9' && *b >= '0' && *b <= '9') {
            char *ea, *eb;
            unsigned long x = strtoul(a, &ea, 10), y = strtoul(b, &eb, 10);
            if (x != y)
                return x < y ? -1 : 1;
            a = ea;
            b = eb;
        } else {
            if (*a != *b)
                return (unsigned char)*a < (unsigned char)*b ? -1 : 1;
            a++;
            b++;
        }
    }
    return (*a != 0) - (*b != 0);
}

static int iface_cmp(const void *x, const void *y)
{
    return natural_cmp(((const sys_iface *)x)->name, ((const sys_iface *)y)->name);
}

int sys_get_interfaces(sys_iface **out, int *count)
{
    struct ifaddrs *list;
    if (getifaddrs(&list) != 0)
        return -1;

    int n = 0, cap = 16;
    sys_iface *arr = calloc((size_t)cap, sizeof *arr);
    if (!arr) {
        freeifaddrs(list);
        return -1;
    }

    for (const struct ifaddrs *ifa = list; ifa; ifa = ifa->ifa_next) {
        sys_iface *ifc = NULL;
        for (int i = 0; i < n; i++) {
            if (strcmp(arr[i].name, ifa->ifa_name) == 0) {
                ifc = &arr[i];
                break;
            }
        }
        if (!ifc) {
            if (n == cap) {
                cap *= 2;
                sys_iface *grown = realloc(arr, (size_t)cap * sizeof *arr);
                if (!grown) {
                    free(arr);
                    freeifaddrs(list);
                    return -1;
                }
                arr = grown;
                memset(arr + n, 0, (size_t)(cap - n) * sizeof *arr);
            }
            ifc = &arr[n++];
            snprintf(ifc->name, sizeof ifc->name, "%s", ifa->ifa_name);
            ifc->admin_up = !!(ifa->ifa_flags & IFF_UP);
            ifc->running = !!(ifa->ifa_flags & IFF_RUNNING);
            ifc->loopback = !!(ifa->ifa_flags & IFF_LOOPBACK);
        }
        add_addr(ifc, ifa);
        sys_platform_iface_link(ifc, ifa);
    }
    freeifaddrs(list);
    sys_platform_iface_post(arr, n);
    qsort(arr, (size_t)n, sizeof *arr, iface_cmp);
    *out = arr;
    *count = n;
    return 0;
}

void sys_username(unsigned uid, char *buf, size_t n)
{
    const struct passwd *pw = getpwuid((uid_t)uid);
    if (pw)
        snprintf(buf, n, "%s", pw->pw_name);
    else
        snprintf(buf, n, "%u", uid);
}

static int route_cmp(const void *x, const void *y)
{
    const sys_route *a = x, *b = y;
    if (a->family != b->family)
        return a->family < b->family ? -1 : 1;

    unsigned char ba[16] = {0}, bb[16] = {0};
    inet_pton(a->family, a->dest, ba);
    inet_pton(b->family, b->dest, bb);
    int c = memcmp(ba, bb, a->family == AF_INET ? 4 : 16);
    if (c)
        return c;
    return a->prefix - b->prefix;
}

void sys_sort_routes(sys_route *routes, int count)
{
    qsort(routes, (size_t)count, sizeof *routes, route_cmp);
}

/* utmpx fields are fixed-width and not always NUL-terminated. */
static void copy_field(char *dst, size_t dstlen, const char *src, size_t srclen)
{
    size_t n = 0;
    while (n < srclen && n + 1 < dstlen && src[n])
        n++;
    memcpy(dst, src, n);
    dst[n] = '\0';
}

int sys_get_users(sys_user **out, int *count)
{
    int n = 0, cap = 8;
    sys_user *arr = calloc((size_t)cap, sizeof *arr);
    const char *mytty = ttyname(STDIN_FILENO);
    time_t now = time(NULL);
    const struct utmpx *u;

    if (!arr)
        return -1;
    if (mytty && strncmp(mytty, "/dev/", 5) == 0)
        mytty += 5;

    setutxent();
    while ((u = getutxent()) != NULL) {
        if (u->ut_type != USER_PROCESS)
            continue;
        if (n == cap) {
            cap *= 2;
            sys_user *grown = realloc(arr, (size_t)cap * sizeof *arr);
            if (!grown) {
                endutxent();
                free(arr);
                return -1;
            }
            arr = grown;
        }
        sys_user *s = &arr[n++];
        memset(s, 0, sizeof *s);
        copy_field(s->line, sizeof s->line, u->ut_line, sizeof u->ut_line);
        copy_field(s->user, sizeof s->user, u->ut_user, sizeof u->ut_user);
        copy_field(s->host, sizeof s->host, u->ut_host, sizeof u->ut_host);
        s->login = (long long)u->ut_tv.tv_sec;
        s->current = mytty && strcmp(mytty, s->line) == 0;

        char dev[64];
        struct stat st;
        snprintf(dev, sizeof dev, "/dev/%.50s", s->line);
        s->idle_secs = stat(dev, &st) == 0 && now >= st.st_atime ? (long long)(now - st.st_atime) : -1;
    }
    endutxent();
    *out = arr;
    *count = n;
    return 0;
}

void sys_free_lines(char **lines, int count)
{
    for (int i = 0; i < count; i++)
        free(lines[i]);
    free(lines);
}

/* Reads a stream keeping only the last max_lines lines (a ring of heap-allocated strings). */
static int read_tail(FILE *f, int max_lines, char ***lines, int *count)
{
    char **ring = calloc((size_t)max_lines, sizeof *ring);
    char *buf = NULL;
    size_t cap = 0;
    long total = 0;

    if (!ring)
        return -1;
    while (getline(&buf, &cap, f) >= 0) {
        buf[strcspn(buf, "\r\n")] = '\0';
        char *copy = strdup(buf);
        if (!copy)
            break;
        free(ring[total % max_lines]);
        ring[total % max_lines] = copy;
        total++;
    }
    free(buf);

    int n = total < max_lines ? (int)total : max_lines;
    char **out = calloc((size_t)n + 1, sizeof *out);
    if (!out) {
        sys_free_lines(ring, max_lines);
        return -1;
    }
    for (int i = 0; i < n; i++) {
        long idx = (total - n + i) % max_lines;
        out[i] = ring[idx];
        ring[idx] = NULL;
    }
    sys_free_lines(ring, max_lines);
    *lines = out;
    *count = n;
    return 0;
}

/* `cmd` must be a fixed string chosen by the program, never user input. Returns 0 only if it exited with status 0. */
int sys_run_lines(const char *cmd, int max_lines, char ***lines, int *count)
{
    FILE *p = popen(cmd, "r");
    if (!p)
        return -1;
    int rc = read_tail(p, max_lines, lines, count);
    int status = pclose(p);
    if (rc != 0)
        return -1;
    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        sys_free_lines(*lines, *count);
        *lines = NULL;
        *count = 0;
        return -1;
    }
    return 0;
}

int sys_tail_file(const char *path, int max_lines, char ***lines, int *count)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    int rc = read_tail(f, max_lines, lines, count);
    fclose(f);
    return rc;
}

int sys_get_info(sys_info *info)
{
    struct utsname u;

    memset(info, 0, sizeof *info);
    if (uname(&u) == 0) {
        snprintf(info->kernel, sizeof info->kernel, "%s %s", u.sysname, u.release);
        snprintf(info->arch, sizeof info->arch, "%s", u.machine);
    }
    if (gethostname(info->hostname, sizeof info->hostname) != 0)
        snprintf(info->hostname, sizeof info->hostname, "localhost");
    if (getloadavg(info->load, 3) < 0)
        info->load[0] = info->load[1] = info->load[2] = 0;
    sys_platform_info(info);
    return 0;
}
