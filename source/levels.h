#ifndef LEVELS_H
#define LEVELS_H

#define LV_W 13
#define LV_H 10
#define LV_MAX_SP 32

/* one level of a level file (the dictionary the editor writes) */
typedef struct {
    char tiles[LV_H][LV_W + 1];     /* 'A' + bits: 1 left, 2 right, 4 up, 8 down; Q/R the citadel */
    int nrows;                      /* rows found in the file */
    int p1x, p1y, p2x, p2y;
    int sp[LV_MAX_SP][2];
    int nsp;
    int red, green, blue;
    int wait;
    char background[40];
    char tileset[16];
    char citadel[16];
    int item;
} Level;

typedef struct {
    Level *lv;
    int n, cap;
} LevelSet;

int  levels_parse(LevelSet *set, const char *xml, int len);
int  levels_load_pack(LevelSet *set, const char *name);          /* "levels/Standard levels" */
int  levels_load_file(LevelSet *set, const char *path);
char *levels_write(const LevelSet *set, int *len);
int  levels_save_file(const LevelSet *set, const char *path);
void levels_free(LevelSet *set);
void levels_copy(LevelSet *dst, const LevelSet *src);
Level *levels_insert(LevelSet *set, int at);                      /* a new empty slot at index at */
void levels_remove(LevelSet *set, int at);

int citadel_number(const char *name);   /* citadel -> 1, citadel2..4 -> 2..4 */

#endif
