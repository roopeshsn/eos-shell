#include "history.h"

#include <stdlib.h>
#include <string.h>

#define HIST_MAX 1000

static char *entries[HIST_MAX];
static int first, count;

void hist_add(const char *line)
{
    char *copy = strdup(line);
    if (!copy)
        return;
    if (count == HIST_MAX) {
        free(entries[first]);
        entries[first] = copy;
        first = (first + 1) % HIST_MAX;
        return;
    }
    entries[(first + count++) % HIST_MAX] = copy;
}

int hist_count(void)
{
    return count;
}

const char *hist_get(int index)
{
    return index >= 0 && index < count ? entries[(first + index) % HIST_MAX] : NULL;
}

void hist_free(void)
{
    for (int i = 0; i < count; i++)
        free(entries[(first + i) % HIST_MAX]);
    first = count = 0;
}
