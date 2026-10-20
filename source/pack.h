#ifndef PACK_H
#define PACK_H
#include <gctypes.h>

/* data/pack.bin, written by tools/convert_assets.py */
enum { PK_TEX = 1, PK_SND = 2, PK_DATA = 3 };
enum { PK_RGBA8 = 0, PK_RGB565 = 1 };

typedef struct {
    char name[40];
    u8 type, fmt;
    u16 w, h, pw, ph, rate, pad;
    u32 offset, size;
    u16 pad2;
} __attribute__((packed)) PackEntry;

void pack_init(void);
const PackEntry *pack_find(const char *name);
const void *pack_data(const PackEntry *e);
int pack_count(void);
const PackEntry *pack_entry(int i);

#endif
