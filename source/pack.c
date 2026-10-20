#include <string.h>
#include <stdio.h>
#include "pack.h"
#include "pack_bin.h"

typedef struct { char magic[4]; u32 count, r0, r1; } PackHead;

static const PackHead *head;
static const PackEntry *table;

void pack_init(void) {
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
