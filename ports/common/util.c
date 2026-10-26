#include <dirent.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include "port.h"

/* copies the level files of <from>/levels that <to>/levels does not have yet */
void port_copy_levels(const char *from, const char *to) {
    char src[320], dst[320];
    snprintf(src, sizeof src, "%s/levels", from);
    snprintf(dst, sizeof dst, "%s/levels", to);
    mkdir(dst, 0777);
    DIR *d = opendir(src);
    if (!d) return;
    struct dirent *e;
    while ((e = readdir(d))) {
        if (e->d_name[0] == '.') continue;
        char a[640], b[640];
        snprintf(a, sizeof a, "%s/%s", src, e->d_name);
        snprintf(b, sizeof b, "%s/%s", dst, e->d_name);
        struct stat st;
        if (stat(b, &st) == 0) continue;
        FILE *in = fopen(a, "rb");
        if (!in) continue;
        FILE *out = fopen(b, "wb");
        if (out) {
            char buf[4096];
            size_t n;
            while ((n = fread(buf, 1, sizeof buf, in)) > 0) fwrite(buf, 1, n, out);
            fclose(out);
        }
        fclose(in);
    }
    closedir(d);
}
