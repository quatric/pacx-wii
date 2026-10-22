#ifndef PACK_H
#define PACK_H
#include "platform.h"

/* data/pack.bin, written by tools/convert_assets.py: linked into the Wii program, a file
   in the Resources of the Mac application (little-endian, with untiled RGBA pictures) */
enum { PK_TEX = 1, PK_SND = 2, PK_DATA = 3 };
enum { PK_RGBA8 = 0, PK_RGB565 = 1 };

typedef struct {
    char name[40];
    u8 type, fmt;
    u16 w, h, pw, ph, rate, pad;
    u32 offset, size;
    u16 pad2;
} __attribute__((packed)) PackEntry;

#ifdef PACX_MAC
int pack_open(const char *path);
#endif
void pack_init(void);
const PackEntry *pack_find(const char *name);
const void *pack_data(const PackEntry *e);
int pack_count(void);
const PackEntry *pack_entry(int i);

#endif
