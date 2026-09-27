#ifndef PAGE_H
#define PAGE_H

#include "run.h"

#include <stdbool.h>

/* A self-contained HTML score viewer with the piece's JSON inside. */
bool page_write(const char *path, const Run *run);

#endif
