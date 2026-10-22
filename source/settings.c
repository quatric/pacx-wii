#ifndef PACX_MAC
#include <gccore.h>
#include <fat.h>
#include <ogc/conf.h>
#endif
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include "settings.h"
#include "pack.h"
#include "gfx.h"

Settings cfg;
#ifdef PACX_MAC
char appDir[256];
#else
char appDir[256] = "sd:/apps/pacx";
#endif
int storageOk;

char *file_read(const char *path, int *len) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *b = malloc(n + 1);
    if (!b) { fclose(f); return NULL; }
    if (fread(b, 1, n, f) != (size_t)n) { fclose(f); free(b); return NULL; }
    fclose(f);
    b[n] = 0;
    if (len) *len = (int)n;
    return b;
}

int file_write(const char *path, const void *data, int len) {
    char tmp[300];
    snprintf(tmp, sizeof tmp, "%s.tmp", path);
    FILE *f = fopen(tmp, "wb");
    if (!f) return 0;
    int ok = fwrite(data, 1, len, f) == (size_t)len;
    ok &= fclose(f) == 0;
    if (!ok) { remove(tmp); return 0; }
    remove(path);
    return rename(tmp, path) == 0;
}

void storage_init(const char *argv0) {
#ifdef PACX_MAC
    /* ~/Library/Application Support/Pac the Man X, shared by the game and the editor */
    (void)argv0;
    const char *home = getenv("HOME");
    snprintf(appDir, sizeof appDir, "%s/Library/Application Support/Pac the Man X", home ? home : "/tmp");
    for (char *s = strchr(appDir + 1, '/'); s; s = strchr(s + 1, '/')) {
        *s = 0;
        mkdir(appDir, 0755);
        *s = '/';
    }
    storageOk = 1;
#else
    storageOk = fatInitDefault();
    if (argv0 && strchr(argv0, ':')) {
        strncpy(appDir, argv0, sizeof appDir - 1);
        char *slash = strrchr(appDir, '/');
        if (slash) *slash = 0;
    }
#endif
    if (storageOk) {
        mkdir(appDir, 0777);
        char p[300];
        snprintf(p, sizeof p, "%s/levels", appDir);
        mkdir(p, 0777);
    }
}

/* ------------------------------------------------------------------ settings */

static void defaults(void) {
    /* defaults.plist of the original */
    cfg.difficulty = 1;
    cfg.musicVol = 0.3f;
    cfg.soundVol = 0.7f;
    cfg.levelFile[0] = 0;
    strcpy(cfg.lastEntry[0], "");
    strcpy(cfg.lastEntry[1], "");
    cfg.tryLevel = 1;
    cfg.tryPlayer = 1;
    cfg.aspect = ASPECT_AUTO;
}

void settings_load(void) {
    defaults();
    char p[300];
    snprintf(p, sizeof p, "%s/settings.txt", appDir);
    char *b = file_read(p, NULL);
    if (!b) return;
    for (char *line = strtok(b, "\n"); line; line = strtok(NULL, "\n")) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        char *v = eq + 1;
        char *cr = strchr(v, '\r');
        if (cr) *cr = 0;
        if (!strcmp(line, "Difficulty")) cfg.difficulty = atoi(v);
        else if (!strcmp(line, "MusicVol")) cfg.musicVol = strtof(v, NULL);
        else if (!strcmp(line, "SoundVol")) cfg.soundVol = strtof(v, NULL);
        else if (!strcmp(line, "LevelFile")) strncpy(cfg.levelFile, v, sizeof cfg.levelFile - 1);
        else if (!strcmp(line, "LastEntry1")) strncpy(cfg.lastEntry[0], v, sizeof cfg.lastEntry[0] - 1);
        else if (!strcmp(line, "LastEntry2")) strncpy(cfg.lastEntry[1], v, sizeof cfg.lastEntry[1] - 1);
        else if (!strcmp(line, "TryLevel")) cfg.tryLevel = atoi(v);
        else if (!strcmp(line, "TryPlayer")) cfg.tryPlayer = atoi(v);
        else if (!strcmp(line, "Aspect")) cfg.aspect = atoi(v);
    }
    free(b);
    if (cfg.musicVol < 0 || cfg.musicVol > 1) cfg.musicVol = 0.3f;
    if (cfg.soundVol < 0 || cfg.soundVol > 1) cfg.soundVol = 0.7f;
}

void settings_save(void) {
    if (!storageOk) return;
    char b[1024], p[300];
    int n = snprintf(b, sizeof b,
                     "Difficulty=%d\nMusicVol=%.2f\nSoundVol=%.2f\nLevelFile=%s\nLastEntry1=%s\nLastEntry2=%s\n"
                     "TryLevel=%d\nTryPlayer=%d\nAspect=%d\n",
                     cfg.difficulty, cfg.musicVol, cfg.soundVol, cfg.levelFile, cfg.lastEntry[0],
                     cfg.lastEntry[1], cfg.tryLevel, cfg.tryPlayer, cfg.aspect);
    snprintf(p, sizeof p, "%s/settings.txt", appDir);
    file_write(p, b, n);
}

static int namecmp(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }

int levelfiles_list(char names[][64], int max) {
    int n = 0;
    names[n++][0] = 0;
    char p[300];
    snprintf(p, sizeof p, "%s/levels", appDir);
    DIR *d = opendir(p);
    if (!d) return n;
    struct dirent *e;
    while ((e = readdir(d)) && n < max) {
        int l = strlen(e->d_name);
        if (l > 6 && l < 63 && !strcasecmp(e->d_name + l - 6, ".plist") && e->d_name[0] != '.') {
            strcpy(names[n++], e->d_name);
        }
    }
    closedir(d);
    qsort(names + 1, n - 1, 64, namecmp);
    return n;
}

void levelfile_display(const char *file, char *out, int max) {
    if (!file[0]) { snprintf(out, max, "Standard Levels"); return; }
    snprintf(out, max, "%s", file);
    int l = strlen(out);
    if (l > 6 && !strcasecmp(out + l - 6, ".plist")) out[l - 6] = 0;
}

/* ------------------------------------------------------------------ highscores */

#define MAX_TABLES 64
typedef struct { char name[80]; HsEntry e[HS_MAX]; int n; } HsTable;
static HsTable tables[MAX_TABLES];
static int ntables;

static HsTable *table(const char *name, int create) {
    for (int i = 0; i < ntables; i++)
        if (!strcmp(tables[i].name, name)) return &tables[i];
    if (!create || ntables >= MAX_TABLES) return NULL;
    HsTable *t = &tables[ntables++];
    memset(t, 0, sizeof *t);
    strncpy(t->name, name, sizeof t->name - 1);
    return t;
}

void hs_load(void) {
    ntables = 0;
    char p[300];
    snprintf(p, sizeof p, "%s/highscores.txt", appDir);
    char *b = file_read(p, NULL);
    if (!b) return;
    char *save;
    for (char *line = strtok_r(b, "\n", &save); line; line = strtok_r(NULL, "\n", &save)) {
        char *f[4];
        int k = 0;
        f[k++] = line;
        for (char *c = line; *c && k < 4; c++)
            if (*c == '\t') { *c = 0; f[k++] = c + 1; }
        if (k < 4) continue;
        HsTable *t = table(f[0], 1);
        if (!t || t->n >= HS_MAX) continue;
        HsEntry *e = &t->e[t->n++];
        strncpy(e->name, f[1], sizeof e->name - 1);
        e->score = atoi(f[2]);
        e->level = atoi(f[3]);
    }
    free(b);
}

void hs_save(void) {
    if (!storageOk) return;
    int cap = 256 + ntables * HS_MAX * 128, n = 0;
    char *b = malloc(cap);
    for (int i = 0; i < ntables; i++)
        for (int j = 0; j < tables[i].n; j++)
            n += snprintf(b + n, cap - n, "%s\t%s\t%d\t%d\n", tables[i].name, tables[i].e[j].name,
                          tables[i].e[j].score, tables[i].e[j].level);
    char p[300];
    snprintf(p, sizeof p, "%s/highscores.txt", appDir);
    file_write(p, b, n);
    free(b);
}

int hs_count(const char *name) {
    HsTable *t = table(name, 0);
    return t ? t->n : 0;
}

const HsEntry *hs_entry(const char *name, int i) {
    HsTable *t = table(name, 0);
    return (t && i >= 0 && i < t->n) ? &t->e[i] : NULL;
}

int hs_score_ranks(const char *name, int score) {
    if (score <= 0) return 0;
    HsTable *t = table(name, 0);
    if (!t) return 1;
    int better = 0;
    for (int i = 0; i < t->n; i++) {
        if (t->e[i].score < score) break;
        if (++better == HS_MAX) return 0;
    }
    return 1;
}

int hs_max_score(const char *name) {
    HsTable *t = table(name, 0);
    int m = 0;
    if (t) for (int i = 0; i < t->n; i++) if (t->e[i].score > m) m = t->e[i].score;
    return m;
}

int hs_add(const char *name, const HsEntry *e) {
    HsTable *t = table(name, 1);
    if (!t) return -1;
    int at = -1;
    for (int i = 0; i < t->n; i++) {
        if (t->e[i].score < e->score) { at = i; break; }
        if (t->e[i].score == e->score && t->e[i].level == e->level && !strcmp(t->e[i].name, e->name)) {
            t->e[i] = *e;
            return i;
        }
    }
    if (at >= 0) {
        int last = t->n < HS_MAX ? t->n : HS_MAX - 1;
        for (int i = last; i > at; i--) t->e[i] = t->e[i - 1];
        t->e[at] = *e;
        if (t->n < HS_MAX) t->n++;
        return at;
    }
    if (t->n < HS_MAX) { t->e[t->n] = *e; return t->n++; }
    return -1;
}

void hs_clear_all(void) { ntables = 0; }

/* ------------------------------------------------------------------ strings */

#define MAX_STR 128
static const char *skeys[MAX_STR], *svals[MAX_STR];
static int nstr;
static char *strbuf;

static void load_lang(const char *lang) {
    char name[32];
    snprintf(name, sizeof name, "strings/%s", lang);
    const PackEntry *e = pack_find(name);
    if (!e) return;
    free(strbuf);
    strbuf = malloc(e->size + 1);
    memcpy(strbuf, pack_data(e), e->size);
    strbuf[e->size] = 0;
    nstr = 0;
    char *save;
    for (char *line = strtok_r(strbuf, "\n", &save); line && nstr < MAX_STR; line = strtok_r(NULL, "\n", &save)) {
        char *eq = strchr(line, '=');
        if (!eq) continue;
        *eq = 0;
        skeys[nstr] = line;
        svals[nstr] = eq + 1;
        nstr++;
    }
}

void strings_init(void) {
#ifdef PACX_MAC
    const char *lang = platform_language();
#else
    int l = CONF_GetLanguage();
    const char *lang = "en";
    if (l == CONF_LANG_GERMAN) lang = "de";
    else if (l == CONF_LANG_FRENCH) lang = "fr";
#endif
    load_lang(lang);
    if (!nstr) load_lang("en");
}

const char *tr(const char *key) {
    for (int i = 0; i < nstr; i++)
        if (!strcmp(skeys[i], key)) return svals[i];
    return key;
}
