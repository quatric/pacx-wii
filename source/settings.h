#ifndef SETTINGS_H
#define SETTINGS_H

/* NSUserDefaults of the original, kept in <app folder>/settings.txt */
typedef struct {
    int difficulty;         /* Difficulty 1 easy .. 4 master */
    float musicVol, soundVol;
    char levelFile[64];     /* LevelFile: "" for the standard levels, else a file in levels/ */
    char lastEntry[2][24];  /* LastEntry1, LastEntry2: the names typed last */
    int tryLevel, tryPlayer;
    int aspect;             /* ASPECT_* */
} Settings;

extern Settings cfg;
extern char appDir[256];    /* sd:/apps/pacx (where boot.dol is) */
extern int storageOk;

void storage_init(const char *argv0);
void settings_load(void);
void settings_save(void);

/* the level file names the game can use: "" (standard) first, then levels/<name>.plist */
int levelfiles_list(char names[][64], int max);
void levelfile_display(const char *file, char *out, int max);   /* "Standard Levels" / file without .plist */

/* MGHighscores: tables of up to 10 entries, keyed "<LevelFile>_<difficulty>" */
#define HS_MAX 10
typedef struct { char name[24]; int score, level; } HsEntry;

void hs_load(void);
void hs_save(void);
int  hs_count(const char *table);
const HsEntry *hs_entry(const char *table, int i);
int  hs_score_ranks(const char *table, int score);   /* scoreRanks:table: */
int  hs_max_score(const char *table);
int  hs_add(const char *table, const HsEntry *e);     /* addEntry:table:, returns the index or -1 */
void hs_clear_all(void);

/* Localizable.strings */
void strings_init(void);
const char *tr(const char *key);

char *file_read(const char *path, int *len);
int file_write(const char *path, const void *data, int len);

#endif
