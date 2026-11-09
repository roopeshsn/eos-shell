#ifndef SYS_H
#define SYS_H

#include <stddef.h>

/*
 * Platform-neutral view of the host. Commands in src/cmds call only this API;
 * OS-specific code lives in sys_darwin.c and sys_linux.c.
 */

#define SYS_MAX_ADDRS 8

typedef struct {
    int family; /* AF_INET or AF_INET6 */
    char addr[48];
    int prefix;
} sys_addr;

typedef struct {
    char name[32];
    int admin_up; /* IFF_UP */
    int running;  /* IFF_RUNNING */
    int loopback;
    unsigned mtu;
    int has_mac;
    char mac[20]; /* EOS style: aabb.ccdd.eeff */
    int naddr;
    sys_addr addrs[SYS_MAX_ADDRS];
    unsigned long long rx_pkts, rx_bytes, rx_errs;
    unsigned long long tx_pkts, tx_bytes, tx_errs;
} sys_iface;

typedef struct {
    char hostname[64];
    char model[96];
    char os[96];
    char kernel[160];
    char arch[65];
    char serial[64];
    unsigned long long uptime_secs;
    unsigned long long mem_total; /* bytes */
    unsigned long long mem_free;  /* bytes, reclaimable included */
    double load[3];
} sys_info;

/* Returns 0 on success. *out is malloc'd (free with free()), sorted by name. */
int sys_get_interfaces(sys_iface **out, int *count);
int sys_get_info(sys_info *info);

typedef struct {
    int family;        /* AF_INET or AF_INET6 */
    char dest[48];
    int prefix;
    char gateway[48];  /* empty when directly connected */
    char iface[32];
    int metric;
} sys_route;

typedef struct {
    char ip[48];
    char mac[20];
    char iface[32];
} sys_arp;

typedef struct {
    int pid;
    int ppid;
    char user[32];
    char state; /* R running, S sleeping, I idle, T stopped, Z zombie */
    unsigned long long rss; /* resident bytes, 0 if not readable */
    double cpu_secs;        /* cumulative user+system time, 0 if not readable */
    char name[64];
} sys_proc;

/* Returns 0 on success; *out is malloc'd and sorted by pid. */
int sys_get_processes(sys_proc **out, int *count);
void sys_username(unsigned uid, char *buf, size_t n);

/* Both return 0 on success with malloc'd arrays (free with free()). Routes are sorted. */
int sys_get_routes(sys_route **out, int *count);
int sys_get_arp(sys_arp **out, int *count);
void sys_sort_routes(sys_route *routes, int count);

/* Logged-in sessions (utmpx). */
typedef struct {
    char line[32];
    char user[32];
    char host[64];
    long long login;     /* epoch seconds */
    long long idle_secs; /* -1 when unknown */
    int current;         /* this shell's own terminal */
} sys_user;
int sys_get_users(sys_user **out, int *count);

/* Power and thermal state. Fields that the host cannot report stay at -1 / empty. */
#define SYS_MAX_SENSORS 32
typedef struct {
    char name[56];
    double value;
    char unit[4]; /* "C" or "RPM" */
} sys_sensor;

typedef struct {
    int has_battery;
    int percent;       /* -1 unknown */
    char state[24];    /* charging, discharging, charged, not charging */
    int on_ac;         /* -1 unknown */
    int cycles;        /* -1 unknown */
    int health_pct;    /* full-charge capacity vs design, -1 unknown */
    double voltage_v;  /* <0 unknown */
    double temp_c;     /* battery temperature, <-200 unknown */
    int nsensors;
    sys_sensor sensors[SYS_MAX_SENSORS];
} sys_env;
int sys_get_environment(sys_env *env);

/*
 * Last `max_lines` lines of the system log in `*lines` (free with sys_free_lines).
 * Returns 0 on success; otherwise -1 with a human-readable reason in err.
 */
int sys_get_log(int max_lines, char ***lines, int *count, char *err, size_t errlen);
void sys_free_lines(char **lines, int count);

/* Helpers for the platform files: run a fixed command / read a file keeping only the last lines. */
int sys_run_lines(const char *cmd, int max_lines, char ***lines, int *count);
int sys_tail_file(const char *path, int max_lines, char ***lines, int *count);

/* Formatting helpers shared by all platforms. */
void sys_format_mac(const unsigned char mac[6], char out[20]);
void sys_format_uptime(unsigned long long secs, char *buf, size_t n);

/* Provided by the platform file. */
struct ifaddrs;
void sys_platform_iface_link(sys_iface *ifc, const struct ifaddrs *ifa);
void sys_platform_iface_post(sys_iface *arr, int n); /* after all interfaces are collected */
void sys_platform_info(sys_info *info);

#endif
