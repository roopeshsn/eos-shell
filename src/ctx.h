#ifndef CTX_H
#define CTX_H

#include <stdio.h>

typedef enum {
    MODE_EXEC,
    MODE_PRIV,
    MODE_CONFIG,
    MODE_CONFIG_IF,
    MODE_COUNT
} cli_mode;

#define M_EXEC      (1u << MODE_EXEC)
#define M_PRIV      (1u << MODE_PRIV)
#define M_CONFIG    (1u << MODE_CONFIG)
#define M_CONFIG_IF (1u << MODE_CONFIG_IF)
#define M_ALL       (M_EXEC | M_PRIV | M_CONFIG | M_CONFIG_IF)
#define M_SHOW      (M_EXEC | M_PRIV | M_CONFIG | M_CONFIG_IF)

typedef struct cli_ctx {
    cli_mode mode;
    char hostname[64];
    char iface[64];
    FILE *out;
    int quit;
} cli_ctx;

#endif
