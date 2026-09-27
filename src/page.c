#include "page.h"

#include "trace.h"

#include <stdio.h>
#include <string.h>

/* Generated at build time from src/score_page.html by cmake/embed.cmake. */
extern const unsigned char score_page_html[];
extern const unsigned long score_page_html_len;

static const char data_marker[] = "/*PIECE_DATA*/";

bool page_write(const char *path, const Run *run) {
    const char *html = (const char *)score_page_html;
    const char *marker = strstr(html, data_marker);
    if (marker == NULL) return false;
    FILE *f = fopen(path, "w");
    if (f == NULL) return false;
    fwrite(html, 1, (size_t)(marker - html), f);
    trace_write_json(f, run);
    const char *rest = marker + sizeof(data_marker) - 1;
    fwrite(rest, 1, strlen(rest), f);
    bool ok = !ferror(f);
    return fclose(f) == 0 && ok;
}
