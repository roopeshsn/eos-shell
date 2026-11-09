#ifndef HISTORY_H
#define HISTORY_H

/* Commands entered this session, oldest first. Kept apart from readline so behaviour is identical with libedit. */
void hist_add(const char *line);
int hist_count(void);
const char *hist_get(int index); /* 0 = oldest; NULL when out of range */
void hist_free(void);

#endif
