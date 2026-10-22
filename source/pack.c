#include <string.h>
#include <stdio.h>
#include "pack.h"
#ifdef PACX_MAC
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>

static const u8 *pack_bin;

int pack_open(const char *path) {
    int fd = open(path, O_RDONLY);
    if (fd < 0) return 0;
    struct stat st;
    if (fstat(fd, &st) < 0) { close(fd); return 0; }
    void *m = mmap(NULL, st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (m == MAP_FAILED) return 0;
    pack_bin = m;
    return 1;
}
#else
#include "pack_bin.h"
#endif

typedef struct { char magic[4]; u32 count, r0, r1; } PackHead;

static const PackHead *head;
static const PackEntry *table;

void pack_init(void) {
#ifdef PACX_MAC
    if (!pack_bin) return;
#endif
    head = (const PackHead *)pack_bin;
    if (memcmp(head->magic, "PXPK", 4) != 0) { head = NULL; return; }
    table = (const PackEntry *)(pack_bin + sizeof(PackHead));
}

int pack_count(void) { return head ? (int)head->count : 0; }
const PackEntry *pack_entry(int i) { return &table[i]; }

const PackEntry *pack_find(const char *name) {
    if (!head) return NULL;
    for (u32 i = 0; i < head->count; i++)
        if (strncmp(table[i].name, name, sizeof table[i].name) == 0) return &table[i];
    return NULL;
}

const void *pack_data(const PackEntry *e) { return pack_bin + e->offset; }
