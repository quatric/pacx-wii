/*
 * PMGame: level building, scoring, the timers and the title, high score, help and credits
 * screens. Each function follows the method of the same name in the original.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "game.h"
#include "actors.h"
#include "audio.h"
#include "input.h"
#include "settings.h"

Game game;

FrameSet *fs_player[16], *fs_ghosts[16], *fs_points[5];
FrameSet *fs_die_part1, *fs_ghost_run, *fs_ghost, *fs_extra;
static FrameSet *fs_pellet, *fs_super_pellet, *fs_background;

static int keysEnabled;     /* updateSettings: the game keys only work in a game */
static int pointsColor;     /* 545a8 */

/* ================================================================== start-up */

void game_init(void) {
    memset(&game, 0, sizeof game);
    game.difficulty = 2;
    game.lastEntry[0] = game.lastEntry[1] = -1;

    static const char *whole[] = {"arrow", "title", "pellet", "barrier", "game_over", "heart",
                                  "ready", "pause", "ending", "spot", "help", "the_end",
                                  "highscore_title", "citadel1", "citadel2", "citadel3", "citadel4"};
    for (unsigned i = 0; i < sizeof whole / sizeof whole[0]; i++) fs_whole(whole[i]);
    fs_pellet = fs_find("pellet");

    fs_extra = fs_split("extra", 32, 32);
    fs_die_part1 = fs_split("die_part1", 32, 32);
    fs_super_pellet = fs_split("super_pellet", 30, 30);
    fs_cycle(fs_super_pellet);
    FrameSet *die2 = fs_split("die_part2", 96, 96);
    fs_dup("die_stars", die2, 0, 14);
    fs_split("pop", 96, 96);
    FrameSet *pl[4] = {fs_split("player1", 32, 32), fs_split("player1_flash", 32, 32),
                       fs_split("player2", 32, 32), fs_split("player2_flash", 32, 32)};
    fs_split("buttons", 48, 48);
    fs_ghost = fs_split("ghost", 32, 32);
    fs_ghost_run = fs_split("ghost_run", 32, 32);
    fs_split("tile", 11, 11);
    fs_split("tile2", 11, 11);
    FrameSet *pts = fs_split("points", 18, 21);
    for (int i = 0; i < 5; i++) fs_points[i] = fs_dup("points_row", pts, i * 8, i * 8 + 7);
    FrameSet *font = fs_split("font", 16, 26);
    FrameSet *small = fs_split("font_small", 8, 13);
    char name[32];
    for (int i = 0; i < 4; i++) {
        snprintf(name, sizeof name, "font%d", i);
        fs_dup(name, font, i * 55, i * 55 + 54);
        snprintf(name, sizeof name, "font_small%d", i);
        fs_dup(name, small, i * 55, i * 55 + 54);
    }
    /* 542e0: four directions of each player sheet, eight frames each, played back and forth */
    for (int s = 0; s < 4; s++)
        for (int d = 0; d < 4; d++) {
            FrameSet *f = fs_dup("player_dir", pl[s], d * 8, d * 8 + 7);
            fs_cycle(f);
            fs_player[s * 4 + d] = f;
        }
    /* 54360: four ghosts, four directions, three frames */
    for (int i = 0; i < 16; i++) fs_ghosts[i] = fs_dup("ghost_dir", fs_ghost, i * 3, i * 3 + 2);

    /* the players collide with the pellets, super pellets, ghosts and extras */
    eng_layer_collides(L(4), L(2));
    eng_layer_collides(L(4), L(3));
    eng_layer_collides(L(4), L(5));
    eng_layer_collides(L(4), L(6));

    levels_load_pack(&game.standard, "levels/Standard levels");
}

/* ================================================================== level data */

static int tile_at(int layer, int x, int y) {
    if (x < 0 || x >= LV_W || y < 0 || y >= LV_H) return 0;
    return game.tiles[layer][x][y];
}

static void set_tile(int layer, int x, int y, int v) {
    if (x < 0 || x >= LV_W || y < 0 || y >= LV_H) return;
    game.tiles[layer][x][y] = v;
}

int game_count_levels(void) { return game.custom.n ? game.custom.n : 25; }
static int is_last_level(void) { return game.levelNr == game_count_levels() - 1; }

int game_next_ghost_time(void) {
    int l = game.levelNr;
    if (l > 22) return 3;
    if (l > 17) return 4;
    if (l > 12) return 5;
    return l < 8 ? 7 : 6;
}

static const LevelSet *editorSet;     /* the editor's levels while one of them is tried */
static int editorReturn;

static void load_level_set(void) {
    game.automaticTimerStart = 45;
    levels_free(&game.custom);
    if (editorSet) {
        levels_copy(&game.custom, editorSet);
        return;
    }
    if (cfg.levelFile[0]) {
        char p[300];
        snprintf(p, sizeof p, "%s/levels/%s", appDir, cfg.levelFile);
        if (!levels_load_file(&game.custom, p)) levels_free(&game.custom);
    }
}

static char tileset[16], background[40];

/* loadStandardLevel: / loadCustomLevel: */
static void load_level(int idx) {
    const Level *l;
    int custom = game.custom.n > 0;
    if (custom) {
        if (idx > game.custom.n - 1) idx = game.custom.n - 1;
        l = &game.custom.lv[idx];
    } else {
        if (idx >= game.standard.n) idx = game.standard.n - 1;
        if (idx < 0) idx = 0;
        l = &game.standard.lv[idx];
    }
    memset(game.tiles, 0, sizeof game.tiles);
    for (int r = 0; r < LV_H; r++)
        for (int c = 0; c < LV_W; c++) game.tiles[0][c][r] = l->tiles[r][c] - 'A';
    set_tile(2, l->p1x, l->p1y, 1);
    set_tile(2, l->p2x, l->p2y, 2);
    for (int i = 0; i < l->nsp; i++) set_tile(1, l->sp[i][0], l->sp[i][1], 1);
    game.tileColor[0] = l->red / 255.0f;
    game.tileColor[1] = l->green / 255.0f;
    game.tileColor[2] = l->blue / 255.0f;
    game.tileColor[3] = 1.0f;
    snprintf(background, sizeof background, "%s", l->background);
    /* the standard levels always use the embossed tiles */
    snprintf(tileset, sizeof tileset, "%s", custom ? l->tileset : "tile");
    if (!fs_find(tileset)) snprintf(tileset, sizeof tileset, "tile");
    game.citadelNr = citadel_number(l->citadel);
}

int game_direction_allowed(int dir, int x, int y) {
    if (x < 0) x = 0;
    else if (x > 12) x = 12;
    if (y < 0) y = 0;
    else if (y > 9) y = 9;
    int t = tile_at(0, x, y);
    if (t > 17) t = 0;
    return (dir & t) != 0;
}

int game_is_citadel_entry(int x, int y) { return game.citadelEntryX == x && game.citadelEntryY == y; }

int game_home_direction(int x, int y) {
    if (x < 0) x = 0;
    else if (x > 12) x = 12;
    if (y < 0) y = 0;
    else if (y > 9) y = 9;
    return tile_at(3, x, y);
}

static void create_home_directions(void) {
    set_tile(3, game.citadelEntryX, game.citadelEntryY + 1, 8);
    for (int pass = 0; pass < 2; pass++) {
        int changes;
        do {
            changes = 0;
            for (int y = 0; y <= 9; y++)
                for (int x = 0; x <= 12; x++) {
                    if (tile_at(3, x, y)) continue;
                    int t = tile_at(0, x, y), v = 0;
                    if (x <= 11 && (t & 2) && tile_at(3, x + 1, y)) v = 2;
                    else if (x > 0 && (t & 1) && tile_at(3, x - 1, y)) v = 1;
                    else if (y <= 8 && (t & 8) && tile_at(3, x, y + 1)) v = 8;
                    else if (y > 0 && (t & 4) && tile_at(3, x, y - 1)) v = 4;
                    else if (pass) {
                        if (x == 12 && (t & 2) && tile_at(3, 0, y)) v = 2;
                        else if (x == 0 && (t & 1) && tile_at(3, 12, y)) v = 1;
                        else if (y == 9 && (t & 8) && tile_at(3, x, 0)) v = 8;
                        else if (y == 0 && (t & 4) && tile_at(3, x, 9)) v = 4;
                    }
                    if (v) {
                        set_tile(3, x, y, v);
                        changes++;
                    }
                }
        } while (changes);
    }
}

/* ================================================================== level building */

static void add_tile_sprite(int x, int y, FrameSet *fs, int frame, int xo, int yo) {
    Sprite *s = spr_create(L(1), K_SPRITE);
    spr_move_to(s, (float)(44 * x + 11 * xo + 34), (float)(44 * y + 11 * yo + 36));
    spr_set_frameset_index(s, fs, frame);
    spr_set_color(s, game.tileColor[0], game.tileColor[1], game.tileColor[2], game.tileColor[3]);
}

typedef struct { int x, y; FrameSet *fs; } TileCtx;

static void emit_tile(void *u, int frame, int xo, int yo) {
    TileCtx *c = u;
    add_tile_sprite(c->x, c->y, c->fs, frame, xo, yo);
}

static void add_pellet_sprites(int x, int y, int type, int right, int down) {
    Sprite *s;
    if (type == 1) {
        s = spr_create(L(2), K_SPRITE);
        spr_move_to(s, (float)(44 * x + 51), (float)(44 * y + 53));
        spr_set_frameset(s, fs_pellet);
    } else if (type == 2) {
        s = spr_create(L(3), K_SPRITE);
        spr_move_to(s, (float)(44 * x + 41), (float)(44 * y + 43));
        spr_set_frameset(s, fs_super_pellet);
        spr_set_frame_speed(s, 15);
    }
    if (right) {
        s = spr_create(L(2), K_SPRITE);
        spr_move_to(s, (float)(44 * x + 73), (float)(44 * y + 53));
        spr_set_frameset(s, fs_pellet);
    }
    if (down) {
        s = spr_create(L(2), K_SPRITE);
        spr_move_to(s, (float)(44 * x + 51), (float)(44 * y + 75));
        spr_set_frameset(s, fs_pellet);
    }
}

static Sprite *create_player(int x, int y, int nr) {
    Sprite *p = spr_create_at(L(4), K_PLAYER, x, y);
    player_start(p, nr, game.mode == MODE_DEMO);
    return p;
}

static void build_level(int players) {
    eng_remove_all();
    game.countExtrasAppeared = 0;
    load_level(game.levelNr);
    game.nextras = 0;
    game.timeForNextGhost = game_next_ghost_time();

    /* the background picture, tiled over the screen */
    char bg[48];
    snprintf(bg, sizeof bg, "bg/%s", background);
    fs_background = fs_find(bg);
    if (!fs_background) fs_background = fs_whole(bg);
    if (fs_background && fs_background->count > 0) {
        int w = fs_background->frames[0].w, h = fs_background->frames[0].h;
        for (int y = 0; y < VIEW_H; y += h)
            for (int x = 0; x < VIEW_W; x += w) {
                Sprite *s = spr_create(LAYER_BACK, K_SPRITE);
                spr_move_to(s, (float)x, (float)y);
                spr_set_frameset(s, fs_background);
            }
    }

    FrameSet *tiles = fs_find(tileset);
    for (int y = 0; y < 10; y++)
        for (int x = 0; x <= 12; x++)
            if (tile_at(0, x, y) == 17) {
                game.citadelEntryX = x;
                game.citadelEntryY = y - 1;
                break;
            }

    for (int y = 0; y < 10; y++)
        for (int x = 0; x < 13; x++) {
            int t = tile_at(0, x, y);
            if (t > 17) t = 0;
            if (t > 15) continue;
            if (t == 0) {
                maze_cell_parts(t, emit_tile, &(TileCtx){x, y, tiles});
                continue;
            }
            int R = t & 2, D = t & 8;
            int sp = tile_at(1, x, y), st = tile_at(2, x, y);
            int down = D && y <= 8 && !(x == game.citadelEntryX && y == game.citadelEntryY);
            int right = R && x < 12;
            int ptype = sp == 1 ? 2 : 1;
            if (st == 1 && players != 2) ptype = 0;
            if (st == 2 && players != 1) ptype = 0;
            add_pellet_sprites(x, y, ptype, right, down);
            if (ptype != 0) {
                game.extraX[game.nextras] = x;
                game.extraY[game.nextras] = y;
                game.nextras++;
            } else if (st == 1 && players <= 1) {
                create_player(44 * x + 40, 44 * y + 42, 1);
            } else if (st == 2 && players != 1) {
                create_player(44 * x + 40, 44 * y + 42, 2);
            }

            maze_cell_parts(t, emit_tile, &(TileCtx){x, y, tiles});
        }

    int ex = game.citadelEntryX, ey = game.citadelEntryY;
    char cname[16];
    snprintf(cname, sizeof cname, "citadel%d", game.citadelNr);
    Sprite *c = spr_create(L(1), K_SPRITE);
    spr_set_frameset_name(c, cname);
    spr_set_color(c, game.tileColor[0], game.tileColor[1], game.tileColor[2], game.tileColor[3]);
    spr_move_to(c, (float)(44 * ex - 10), (float)(44 * ey + 80));
    set_tile(0, ex - 1, ey + 1, 10);
    set_tile(0, ex, ey + 1, 15);
    set_tile(0, ex + 1, ey + 1, 9);
    set_tile(0, ex - 1, ey + 2, 4);
    set_tile(0, ex, ey + 2, 4);
    set_tile(0, ex + 1, ey + 2, 4);
    Sprite *b = spr_create(L(7), K_SPRITE);
    spr_set_frameset_name(b, "barrier");
    spr_move_to(b, (float)(44 * ex + 36), (float)(44 * ey + 76));
    for (int i = 0; i < 4; i++) {
        Sprite *g = spr_create(L(5), K_GHOST);
        ghost_start(g, i, ex, ey);
    }

    /* black borders over the parts of the maze that wrap around */
    Sprite *r = spr_create(L(10), K_SPRITE);
    spr_set_empty(r, 640, 36, 0, 0, 0, 1);
    r = spr_create(L(10), K_SPRITE);
    r->y = 476;
    spr_set_empty(r, 640, 4, 0, 0, 0, 1);
    r = spr_create(L(10), K_SPRITE);
    spr_set_empty(r, 34, 480, 0, 0, 0, 1);
    r = spr_create(L(10), K_SPRITE);
    r->x = 606;
    spr_set_empty(r, 34, 480, 0, 0, 0, 1);

    create_home_directions();

    for (int d = 0; d < 8; d++)
        for (int p = 1; p <= 2; p++) {
            Sprite *s = spr_create(L(11), K_DIGIT);
            digit_start(s, d, p, &game.score[p - 1]);
        }
    for (int d = 0; d < 2; d++) {
        Sprite *s = spr_create(L(11), K_DIGIT);
        digit_start(s, d, 0, &game.levelNr);
    }
    for (int n = 1, y = 46; y != 446; n++, y += 20) {
        Sprite *h = spr_create_at(L(12), K_HEART, 10, y);
        heart_start(h, n, &game.lives[0]);
        h = spr_create_at(L(12), K_HEART, 614, y);
        heart_start(h, n, &game.lives[1]);
    }
    Sprite *i1 = spr_create(L(11), K_SPRITE);
    spr_set_frameset_name(i1, "player1");
    spr_set_frame_index(i1, 11);
    spr_move_to(i1, 38, 4);
    Sprite *i2 = spr_create(L(11), K_SPRITE);
    spr_set_frameset_name(i2, "player2");
    spr_set_frame_index(i2, 4);
    spr_move_to(i2, 570, 4);

    Sprite *rd = spr_create(L(15), K_SPRITE);
    spr_set_frameset_name(rd, "ready");
    spr_center_on_view(rd);
    rd->y = 224;
    spr_set_duration(rd, 1.2f);

    if (game.playerCanJoin) {
        /* "PRESS RETURN" in the original; the join button on the Wii is A */
#if defined(PACX_MAC) || defined(PACX_DESKTOP)
        static const char text[] = "PRESS RETURN";
#elif defined(PACX_3DS)
        static const char text[] = "PRESS L";
#else
        static const char text[] = "PRESS A";
#endif
        int x = players == 1 ? 360 : 90;
        for (int i = 0; text[i]; i++, x += 16) {
            if (text[i] == ' ') continue;
            Sprite *s = spr_create(L(13), K_SPRITE);
            spr_move_to(s, (float)x, 7);
            spr_set_frameset_name(s, "font3");
            spr_set_frame_index(s, text[i] - 55);
        }
    }
}

/* ================================================================== settings */

void game_update_settings(void) {
    keysEnabled = game.mode == MODE_GAME;
    mus_set_volume(cfg.musicVol);
    snd_set_volume(cfg.soundVol);
}

void game_table_name(char *out, int max) {
    /* highscoreTableName: "<LevelFile>_<Difficulty>" */
    game.difficulty = cfg.difficulty;
    snprintf(out, max, "%s_%d", cfg.levelFile[0] ? cfg.levelFile : "Standard levels.plist", cfg.difficulty);
}

/* ================================================================== players and keys */

int game_player_nr(void) {
    if (layer_count(L(4)) == 1) return layer_at(L(4), 0)->u.pl.playerNr;
    return 0;
}

Sprite *game_player(int nr) {
    if (layer_count(L(4)) == 1) return layer_at(L(4), 0);
    for (int i = 0; i < layer_count(L(4)); i++) {
        Sprite *s = layer_at(L(4), i);
        if (s->u.pl.playerNr == nr) return s;
    }
    return NULL;
}

int game_key_down(int player, int dir) {
    if (!keysEnabled) return 0;
    int k;
    switch (dir) {
    case 1: k = 0; break;
    case 2: k = 1; break;
    case 4: k = 2; break;
    case 8: k = 3; break;
    default: return 0;
    }
    if (player == 2) return in_key(KEY_LEFT2 + k);
    if (player == 1) return in_key(KEY_LEFT1 + k);
    return in_key(KEY_LEFT1 + k) || in_key(KEY_LEFT2 + k);
}

int game_moving_allowed(void) {
    if (game.moveDelay == 0) return 1;
    if (game.mode != MODE_DEMO) return 0;
    return game.gameOver != 0;
}

static void set_move_delay(int secs) { game.moveDelay = secs * FPS; }

/* ================================================================== scoring */

void game_add_score(int pts, Sprite *player) {
    int i = player->u.pl.playerNr - 1;
    if (i < 0 || i > 1) return;
    int old = game.score[i];
    game.score[i] = (pts << (player_double_score(player) ? 1 : 0)) + game.score[i];
    if (game.score[i] / 25000 != old / 25000) {
        if (game.mode == MODE_GAME) snd_play(SND_EXTRA_LIFE);
        game.lives[i]++;
    }
}

void game_create_points(int pts, Sprite *player) {
    static const int widths[8] = {16, 12, 16, 16, 18, 16, 16, 16};
    int px = spr_px(player), py = spr_py(player);
    char s[20];
    snprintf(s, sizeof s, "%d", pts << (player_double_score(player) ? 1 : 0));
    int total = 0;
    for (char *c = s; *c; c++) {
        int d = (*c == '8' ? '7' : *c) - '0';
        total += widths[d & 7];
    }
    int x = px - total / 2 + 16;
    for (int i = 0; s[i]; i++) {
        int d = (s[i] == '8' ? '7' : s[i]) - '0';
        Sprite *p = spr_create_at(L(14), K_POINTS, x, py);
        points_start(p, d, pointsColor, i);
        x += widths[d & 7];
    }
    pointsColor = pointsColor + 1 == 5 ? 0 : pointsColor + 1;
}

void game_super_pellet_eaten(void) {
    game.ghostsEaten = 0;
    for (int i = 0; i < layer_count(L(5)); i++) ghost_start_running_away(layer_at(L(5), i));
    if (game.mode == MODE_GAME && !snd_is_playing(SND_SCARED)) snd_play_loop(SND_SCARED);
}

void game_ghost_eaten(Sprite *player) {
    static const int scores[10] = {200, 400, 500, 800, 1000, 2000, 3000, 4000, 5000, 10000};
    static const int table[4][4] = {{0, 1, 3, 5}, {1, 4, 5, 7}, {4, 5, 7, 8}, {4, 6, 8, 9}};
    set_move_delay(1);
    int l = game.levelNr;
    int row = (l > 6) + (l > 12) + (l > 18);
    int n = game.ghostsEaten++;
    int flat = row * 4 + n;
    if (flat > 15) flat = 15;
    int pts = scores[table[flat / 4][flat % 4]];
    game_add_score(pts, player);
    game_create_points(pts, player);
    if (game.mode == MODE_GAME && !snd_is_playing(SND_RUNNING_HOME)) snd_play_loop(SND_RUNNING_HOME);
}

void game_player_starts_dying(int nr) {
    set_move_delay(3);
    if (game.mode == MODE_GAME && game.lives[nr - 1] > 0) game.lives[nr - 1]--;
    for (int i = 0; i < layer_count(L(5)); i++) {
        Sprite *g = layer_at(L(5), i);
        g->visible = 0;
        ghost_send_back_to_start(g);
    }
    game.timeForNextGhost = game_next_ghost_time();
    if (game.lives[0] == 0 && game.lives[1] == 0) game.playerCanJoin = 0;
}

void game_player_dying_completed(Sprite *player) {
    game_sprite_pops(player, 1);
    if (game.lives[0] == 0 && game.lives[1] == 0) {
        layer_remove_all(L(9));
        return;
    }
    set_move_delay(2);
    if (game.lives[player->u.pl.playerNr - 1] > 0) player_move_to_start(player);
    for (int i = 0; i < layer_count(L(5)); i++) {
        Sprite *g = layer_at(L(5), i);
        spr_set_delay(g, 1.0f);
        g->color[3] = 0.7f;
    }
}

void game_sprite_pops(Sprite *s, int isPlayer) {
    Sprite *p = spr_create(L(8), K_SPRITE);
    spr_move_to(p, s->x + -32.0f, -32.0f + s->y);
    spr_set_frameset_name(p, "die_stars");
    spr_set_frame_speed(p, 30);
    p->deleteAfterAnimation = 1;
    if (!isPlayer && !game.gameOver && game.mode == MODE_GAME) snd_play(SND_POP);
}

/* ================================================================== starting and ending */

static void play_music(void) {
    if (game.mode != MODE_GAME || mus_is_playing()) return;
    mus_play(rnd(0, 3), 1);
}

static void start_new_game(int level, int mode, int players) {
    if (mode == MODE_GAME) mus_stop();
    game.levelNr = level;
    game.mode = mode;
    int d = cfg.difficulty;
    game.difficulty = (d >= 1 && d <= 4) ? d : 2;
    if (game.mode == MODE_GAME && players == 0 && game.difficulty == 4) {
        game.alertPending = 1;      /* NoTwoPlayer */
        return;
    }
    load_level_set();
    game_update_settings();
    game.timeForNextGhost = game_next_ghost_time();
    game.gameOver = 0;
    game.levelCompleted = 0;
    game.gameFinished = 0;
    game.automaticTimerRun = 0;
    int lives;
    if (game.mode == MODE_DEMO) {
        game.levelNr = rnd(0, game.custom.n ? game.custom.n - 1 : 24);
        game.automaticTimerRun = 45;
        game.playerCanJoin = 0;
        game.difficulty = rnd(1, 4);
        players = rnd(game.difficulty == 4 ? 1 : 0, 2);
        lives = 5;
    } else {
        game.playerCanJoin = players != 0 && game.difficulty != 4;
        lives = 3;
    }
    set_move_delay(1);
    build_level(players);
    game.score[0] = game.score[1] = 0;
    game.lives[0] = game.lives[1] = 0;
    if ((unsigned)players <= 1) game.lives[0] = lives;
    if ((players & ~2) == 0) game.lives[1] = lives;
    if (game.mode == MODE_DEMO) return;
    play_music();
}

static Sprite *black_fade(void) {
    Sprite *s = spr_create(L(15), K_SPRITE);
    spr_set_empty(s, 640, 480, 0, 0, 0, 1);
    spr_fadein(s, 3.0f);
    return s;
}

static void show_credits(void);
static void check_highscore_made(void);

void game_over(void) {
    game.gameOver = 1;
    switch (game.mode) {
    case MODE_HIGHSCORES:
    case MODE_CREDITS:
        game_show_splash();
        return;
    case MODE_HELP:
        show_credits();
        return;
    case MODE_GAME:
        mus_stop();
        /* fall through */
    case MODE_DEMO:
        break;
    default:
        return;
    }
    if (game.tryLevel) {
        set_move_delay(1);
    } else if (game.mode == MODE_DEMO) {
        set_move_delay(3);
        black_fade();
    } else if (game.levelNr >= 21 && game.levelCompleted && is_last_level()) {
        mus_play(MUS_ENDING, 0);
        eng_remove_all();
        Sprite *e = spr_create(LAYER_BACK, K_SPRITE);
        spr_set_frameset_name(e, "ending");
        game.gameFinished = 1;
        set_move_delay(57);
    } else {
        snd_play(SND_GAMEOVER);
        set_move_delay(7);
        black_fade();
        Sprite *g = spr_create(L(15), K_SPRITE);
        spr_set_frameset_name(g, "game_over");
        spr_center_on_view(g);
        g->y = 224;
    }
    if (game.mode == MODE_DEMO) return;
    layer_remove_all(L(5));
    layer_remove_all(L(4));
}

/* ================================================================== timers */

static void check_continuous_sounds(void) {
    if (game.mode != MODE_GAME) return;
    int anyHome = 0, anyScared = 0;
    for (int i = 0; i < layer_count(L(5)); i++) {
        int st = layer_at(L(5), i)->u.gh.state;
        if (st == 4) anyHome = 1;
        else if (st == 3 || st == 5) anyScared = 1;
    }
    if (!game.levelCompleted && anyHome) {
        if (!snd_is_playing(SND_RUNNING_HOME)) snd_play(SND_RUNNING_HOME);
    } else {
        snd_stop(SND_RUNNING_HOME);
    }
    if (!game.levelCompleted && anyScared) {
        if (!snd_is_playing(SND_SCARED)) snd_play(SND_SCARED);
    } else {
        snd_stop(SND_SCARED);
    }
}

static void check_if_ghost_must_come_out(void) {
    if (game.moveDelay > 0) return;
    int n = layer_count(L(5));
    for (int i = 0; i < n; i++)
        if (layer_at(L(5), i)->u.gh.state == 5) {
            if (game.timeForNextGhost >= 3) game.timeForNextGhost = 2;
            break;
        }
    if (--game.timeForNextGhost > 0) return;
    game.timeForNextGhost = game_next_ghost_time();
    for (int i = 0; i < n; i++) {
        Sprite *g = layer_at(L(5), i);
        if (g->u.gh.state == 5) {
            game.timeForNextGhost = 2;
            ghost_start_running_away(g);
            return;
        }
    }
    for (int i = 0; i < n; i++) {
        Sprite *g = layer_at(L(5), i);
        if (g->u.gh.state == 1) {
            ghost_start_hunting(g);
            if (game.mode == MODE_GAME) snd_play(SND_GHOST_STARTS);
            return;
        }
    }
}

static void check_if_extra_must_appear(void) {
    if (game.gameFinished || game.gameOver) return;
    if (layer_count(L(6)) >= layer_count(L(4))) return;
    if (layer_count(L(2)) < 60) return;
    if (layer_count(L(3)) <= 0) return;
    if (game.countExtrasAppeared > 4) return;
    if (rnd(0, 6) != 0) return;
    if (game.nextras <= 0) return;
    int i = rnd(0, game.nextras - 1);
    int x = game.extraX[i], y = game.extraY[i];
    if (!game_direction_allowed(1, x, y) && !game_direction_allowed(2, x, y) &&
        !game_direction_allowed(4, x, y) && !game_direction_allowed(8, x, y))
        return;
    int px = 44 * x + 40, py = 44 * y + 42;
    for (int k = 0; k < layer_count(L(4)); k++) {
        Sprite *p = layer_at(L(4), k);
        if (abs(px - spr_px(p)) < 100) return;
        if (abs(py - spr_py(p)) < 100) return;
    }
    Sprite *arrow = NULL;
    if (game.difficulty == 4) {
        arrow = spr_create(L(11), K_SPRITE);
        spr_set_frameset_name(arrow, "arrow");
    }
    Sprite *e = spr_create_at(L(6), K_EXTRA, px, py);
    extra_start(e, rnd(0, 4), arrow);
    if (game.mode == MODE_GAME) snd_play(SND_EXTRA_APPEAR);
    game.countExtrasAppeared++;
}

void game_timer_changed(void) {
    if (game.mode == MODE_SPLASH) {
        if (game.splashTimer > 0 && --game.splashTimer == 0 && !mus_is_playing()) {
            mus_set_volume(cfg.musicVol);
            mus_play(MUS_TITLE, 0);
        }
        if (game.automaticTimerStart > 0 && --game.automaticTimerStart == 0)
            start_new_game(0, MODE_DEMO, 0);
    }
    if (game.automaticTimerRun > 0 && game.mode <= 6 && ((0x74 >> game.mode) & 1)) {
        if (--game.automaticTimerRun == 0) game_over();
    }
    if (game.mode == MODE_GAME || game.mode == MODE_DEMO) {
        check_if_ghost_must_come_out();
        check_if_extra_must_appear();
    }
}

static void next_level(void) {
    game.timeForNextGhost = game_next_ghost_time();
    game.levelCompleted = 0;
    game.levelNr++;
    set_move_delay(1);
    build_level(game_player_nr());
    play_music();
}

void game_frame_finished(void) {
    if (game.mode != MODE_GAME && game.mode != MODE_DEMO) return;

    if (game.gameFinished == 1 && rnd(0, FPS / 10) == 0) {
        /* fireworks over the ending picture */
        Sprite *p = spr_create(L(9), K_SPRITE);
        spr_set_frameset_name(p, "pop");
        spr_set_frame_speed(p, 30);
        p->deleteAfterAnimation = 1;
        switch (rnd(0, 5)) {
        case 0: spr_set_color(p, 1, 1, 1, 1); break;
        case 1: spr_set_color(p, 0, 1, 0, 1); break;
        case 2: spr_set_color(p, 1, 0, 0, 1); break;
        case 3: spr_set_color(p, 1, 1, 0, 1); break;
        case 4: spr_set_color(p, 1, 0, 1, 1); break;
        case 5: spr_set_color(p, 0, 1, 1, 1); break;
        }
        int x = rnd(0, 544);
        int y = rnd(0, 384);
        spr_move_to(p, (float)x, (float)y);
    }

    int nr = game_player_nr();
    if (game.mode == MODE_GAME && game.playerCanJoin && in_join(nr == 1 ? 2 : 1)) {
        for (int y = 0; y < 10 && game.playerCanJoin; y++)
            for (int x = 0; x < 13; x++) {
                int st = tile_at(2, x, y);
                if (st == 1 && nr == 2) {
                    game.lives[0] = 3;
                    create_player(44 * x + 40, 44 * y + 42, 1);
                    game.playerCanJoin = 0;
                    break;
                }
                if (st == 2 && nr == 1) {
                    game.lives[1] = 3;
                    create_player(44 * x + 40, 44 * y + 42, 2);
                    game.playerCanJoin = 0;
                    break;
                }
            }
        layer_remove_all(L(13));
    }

    if (game.moveDelay > 0) {
        game.moveDelay--;
        if (game.gameFinished == 1 && game.moveDelay == FPS * 10) {
            game.gameFinished++;
            eng_remove_all();
            Sprite *e = spr_create(LAYER_BACK, K_SPRITE);
            spr_set_frameset_name(e, "the_end");
            spr_center_on_view(e);
        }
        if (game.moveDelay == 0) {
            if (game.gameOver) {
                check_highscore_made();
                game_show_splash();
                return;
            }
            if (game.levelCompleted) {
                if (game.tryLevel || is_last_level()) game_over();
                else next_level();
                return;
            }
        }
    }

    if (!game.gameOver && !game.levelCompleted && layer_count(L(2)) == 0 && layer_count(L(3)) == 0) {
        game.levelCompleted = 1;
        set_move_delay(3);
        mus_stop();
        if (game.mode == MODE_GAME) snd_play(SND_LEVEL_CLEAR);
    }
    check_continuous_sounds();
}

/* ================================================================== high scores */

static void check_highscore_made(void) {
    if (game.mode != MODE_GAME || game.tryLevel) return;
    char t[80];
    game_table_name(t, sizeof t);
    int r1 = hs_score_ranks(t, game.score[0]);
    int r2 = hs_score_ranks(t, game.score[1]);
    if (r1 && r2) game.nameInputPending = 3;
    else if (r1) game.nameInputPending = 1;
    else if (r2) game.nameInputPending = 2;
}

static void show_highscores(void);

void game_enter_new_score(int player) {
    HsEntry e;
    memset(&e, 0, sizeof e);
    e.score = game.score[player - 1];
    snprintf(e.name, sizeof e.name, "%s", cfg.lastEntry[player - 1]);
    e.level = game.levelNr;
    char t[80];
    game_table_name(t, sizeof t);
    if (strcmp(t, game.lastTable)) {
        game.lastEntry[0] = game.lastEntry[1] = -1;
        snprintf(game.lastTable, sizeof game.lastTable, "%s", t);
    }
    int at = hs_add(t, &e);
    int other = 2 - player;
    if (at >= 0 && game.lastEntry[other] >= at) {
        if (++game.lastEntry[other] >= HS_MAX) game.lastEntry[other] = -1;
    }
    game.lastEntry[player - 1] = at;
    hs_save();
    game.mode = MODE_NONE;
    show_highscores();
}

/* createCharSprite:x:y:flashing: */
static void create_char(int c, int col, int row, int flashing) {
    int xstep, ystep, x0, y0, ady, adx;
    const char *font;
    if (game.mode == MODE_CREDITS) {
        xstep = 16; ystep = 50; x0 = 20; y0 = 50; ady = 24; adx = 4;
        font = (row & 1) ? "font0" : "font2";
    } else if (game.mode == MODE_HELP) {
        xstep = 8; ystep = 38; y0 = 96; ady = 14; adx = 2;
        x0 = (row & 1) ? 100 : 40;
        font = (row & 1) ? "font_small1" : "font_small2";
    } else {
        xstep = 16; ystep = 34; x0 = 20; y0 = 90; ady = 24; adx = 4;
        font = row == 10 ? "font1" : (row & 1) ? "font0" : "font2";
    }
    int x = xstep * col + x0, y = ystep * row + y0;
    Sprite *s = spr_create_at(L(7), K_CHAR, x, y);
    spr_set_frameset_name(s, font);
    char_move_to_start(s);
    if (flashing) spr_set_flash(s, 0.5f);
    int accent;
    spr_set_frame_index(s, char_glyph(c, &accent));
    if (accent == 3 || accent == 2) {
        Sprite *a = spr_create_at(L(7), K_CHAR, accent == 3 ? x - 1 : (x | 1), y - ady);
        spr_set_frameset_name(a, font);
        spr_set_frame_index(a, 49);
        char_move_to_start(a);
        if (flashing) spr_set_flash(a, 0.5f);
        if (accent == 3) a->flipped = 1;
    } else if (accent == 1) {
        for (int k = -1; k <= 1; k += 2) {
            Sprite *a = spr_create_at(L(7), K_CHAR, x + k * adx, y - ady);
            spr_set_frameset_name(a, font);
            spr_set_frame_index(a, 48);
            char_move_to_start(a);
            if (flashing) spr_set_flash(a, 0.5f);
        }
    }
}

/* writes a UTF-8 string from column col on, skipping spaces */
static void create_text(const char *s, int col, int row, int flashing) {
    const unsigned char *p = (const unsigned char *)s;
    for (int i = 0; *p; i++) {
        int c = *p++;
        if (c >= 0xC0 && (*p & 0xC0) == 0x80) c = ((c & 0x1F) << 6) | (*p++ & 0x3F);
        if (c != ' ') create_char(c, col + i, row, flashing);
    }
}

static int utf8_len(const char *s) {
    int n = 0;
    for (; *s; s++)
        if ((*s & 0xC0) != 0x80) n++;
    return n;
}

static void show_highscores(void) {
    if (game.mode == MODE_HIGHSCORES) return;
    game.mode = MODE_HIGHSCORES;
    game.automaticTimerRun = 15;
    eng_remove_all();
    game_update_settings();
    Sprite *t = spr_create(LAYER_BACK, K_SPRITE);
    spr_set_frameset_name(t, "highscore_title");
    spr_center_on_view(t);
    t->y = 10;

    char table[80];
    game_table_name(table, sizeof table);
    if (strcmp(table, game.lastTable)) game.lastEntry[0] = game.lastEntry[1] = -1;
    int n = hs_count(table);
    for (int r = 0; r < 10; r++) {
        int flash = n > 0 && (r == game.lastEntry[0] || r == game.lastEntry[1]);
        if (r < 9) {
            create_char('0', 0, r, flash);
            create_char('1' + r, 1, r, flash);
        } else {
            create_char('1', 0, r, flash);
            create_char('0', 1, r, flash);
        }
        create_char('.', 2, r, flash);
        if (n == 0) {
            const char *line = r == 0 || r == 9 ? "+-----------------------------+"
                               : r == 4         ? "|            EMPTY            |"
                                                : "|                             |";
            create_text(line, 4, r, 0);
            continue;
        }
        const HsEntry *e = hs_entry(table, r);
        if (!e) continue;
        char name[24];
        snprintf(name, sizeof name, "%.19s", e->name);
        create_text(name, 4, r, flash);
        char buf[16];
        snprintf(buf, sizeof buf, "%8d", e->score);
        create_text(buf, 26, r, flash);
        snprintf(buf, sizeof buf, "%2d", e->level + 1);
        create_text(buf, 35, r, flash);
    }
    char file[64], line[96];
    levelfile_display(cfg.levelFile, file, sizeof file);
    static const char *names[4] = {"LevelEasy", "LevelNormal", "LevelHard", "LevelMaster"};
    int d = cfg.difficulty >= 1 && cfg.difficulty <= 4 ? cfg.difficulty : 2;
    snprintf(line, sizeof line, "%s - %s", file, tr(names[d - 1]));
    create_text(line, (40 - utf8_len(line)) / 2, 10, 0);
    eng_start_fadein(0.5f);
}

/* ================================================================== help and credits */

static void show_help(void) {
    if (game.mode == MODE_HELP) return;
    game.mode = MODE_HELP;
    game.automaticTimerRun = 25;
    eng_remove_all();
    game_update_settings();
    Sprite *t = spr_create(L(7), K_SPRITE);
    spr_set_frameset_name(t, "help");
    spr_center_on_view(t);
    t->y = 10;
    char key[16];
    for (int i = 0; i < 10; i++) {
        snprintf(key, sizeof key, "Help%d", i + 1);
        create_text(tr(key), 7, i, 0);
    }
    Sprite *s = spr_create(L(7), K_SPRITE);
    spr_set_frameset(s, fs_player[1]);
    spr_set_frame_speed(s, 50);
    spr_move_to(s, 40, 86);
    s = spr_create(L(7), K_SPRITE);
    spr_set_frameset(s, fs_player[9]);
    spr_set_frame_speed(s, 50);
    spr_move_to(s, 100, 124);
    for (int x = 28; x != 94; x += 22) {
        s = spr_create(L(7), K_SPRITE);
        spr_move_to(s, (float)x, 174);
        spr_set_frameset_name(s, "pellet");
    }
    s = spr_create(L(7), K_SPRITE);
    spr_move_to(s, 103, 202);
    spr_set_frameset_name(s, "super_pellet");
    spr_set_frame_speed(s, 15);
    s = spr_create(L(7), K_SPRITE);
    spr_set_frameset(s, fs_ghosts[4]);
    spr_set_frame_speed(s, 5);
    spr_move_to(s, 40, 238);
    for (int i = 0, f = 1, y = 276; f < 16; i++, f += 3, y += 38) {
        s = spr_create(L(7), K_SPRITE);
        spr_move_to(s, (i & 1) ? 40.0f : 100.0f, (float)y);
        spr_set_frameset_name(s, "extra");
        spr_set_frame_index(s, f);
    }
    eng_start_fadein(0.5f);
}

static void show_credits(void) {
    if (game.mode == MODE_CREDITS) return;
    game.mode = MODE_CREDITS;
    game.automaticTimerRun = 25;
    eng_remove_all();
    game_update_settings();
    char key[16];
    for (int i = 0; i < 8; i++) {
        snprintf(key, sizeof key, "Credits%d", i + 1);
        const char *s = tr(key);
        create_text(s, 18 - utf8_len(s) / 2, i, 0);
    }
}

/* ================================================================== splash */

void game_show_splash(void) {
    if (game.mode == MODE_SPLASH) return;
    game.mode = MODE_SPLASH;
    anim.running = 1;       /* left paused when the game was quit from the pause screen */
    if (editorSet) {
        editorSet = NULL;
        editorReturn = 1;
        levels_free(&game.custom);
    }
    if (mus_current() != MUS_TITLE) mus_stop();
    snd_stop_all();
    game.automaticTimerStart = 45;
    eng_remove_all();
    Sprite *t = spr_create(LAYER_BACK, K_SPRITE);
    spr_set_frameset_name(t, "title");
    static const int pos[6][2] = {{103, 374}, {174, 384}, {254, 389}, {338, 389}, {418, 384}, {489, 374}};
    for (int i = 0; i < 6; i++) {
        Sprite *b = spr_create_at(L(0), K_BUTTON, pos[i][0], pos[i][1]);
        spr_set_frameset_name(b, "buttons");
        spr_set_frame_index(b, i);
    }
    anim.fading = 0;
    eng_start_fadein(0.5f);
    game.splashTimer = 4;
    game_update_settings();
}

/* ================================================================== pause and events */

int game_is_paused(void) { return game.mode == MODE_GAME && !anim.running; }

void game_set_paused(int paused) {
    if (game.mode != MODE_GAME) return;
    if (paused != anim.running) return;
    if (paused) {
        if (game.gameFinished || game.gameOver) return;
        Sprite *b = spr_create(L(15), K_SPRITE);
        spr_set_empty(b, 640, 480, 0, 0, 0, 0.627f);
        Sprite *p = spr_create(L(15), K_SPRITE);
        spr_set_frameset_name(p, "pause");
        spr_center_on_view(p);
        p->y = 224;
        snd_stop(SND_RUNNING_HOME);
        snd_stop(SND_SCARED);
        mus_stop();
        anim.running = 0;
    } else {
        game_update_settings();
        mus_resume();
        layer_remove_all(L(15));
        anim.running = 1;
        check_continuous_sounds();
    }
}

void game_try_editor_level(const LevelSet *set, int level, int players) {
    editorSet = set;
    game.tryLevel = 1;
    start_new_game(level, MODE_GAME, players);
}

int game_take_editor_return(void) {
    int r = editorReturn;
    editorReturn = 0;
    return r;
}

static void check_mouse_down(float x, float y) {
    switch (game.mode) {
    case MODE_SPLASH: {
        Sprite *b = layer_sprite_at(L(0), (int)x, (int)y, 4);
        if (b && b->kind == K_BUTTON) button_press(b);
        break;
    }
    case MODE_HIGHSCORES:
    case MODE_DEMO:
    case MODE_HELP:
    case MODE_CREDITS:
        game_show_splash();
        break;
    }
}

void game_handle_event(const Event *e) {
    switch (e->type) {
    case EV_MOUSE:
        check_mouse_down(e->x, e->y);
        return;
    case EV_KEY:
        if (game.mode == MODE_HIGHSCORES || game.mode == MODE_DEMO || game.mode == MODE_CREDITS) {
            game_show_splash();
        } else if (game.mode == MODE_SPLASH) {
            return;
        } else if (game.mode == MODE_HELP) {
            show_credits();
        } else if (game.keyEvent == KEY_CANCEL) {
            check_highscore_made();
            game_show_splash();
        } else if (game.mode == MODE_GAME && game.keyEvent == KEY_PAUSE) {
            game_set_paused(anim.running);
        }
        return;
    case EV_DEACTIVATE:
        game_set_paused(1);
        return;
    case EV_NEW_GAME:
        game.tryLevel = 0;
        start_new_game(0, MODE_GAME, 0);
        return;
    case EV_NEW_GAME1:
        game.tryLevel = 0;
        start_new_game(0, MODE_GAME, 1);
        return;
    case EV_NEW_GAME2:
        game.tryLevel = 0;
        start_new_game(0, MODE_GAME, 2);
        return;
    case EV_PAUSE:
        game_set_paused(1);
        return;
    case EV_RESUME:
        game_set_paused(0);
        return;
    case EV_HELP:
        show_help();
        return;
    case EV_HIGHSCORES:
        show_highscores();
        return;
    case EV_CLEAR_SCORES:
        hs_clear_all();
        hs_save();
        game.mode = MODE_NONE;
        show_highscores();
        return;
    case EV_NEW_SCORE1:
        game_enter_new_score(1);
        return;
    case EV_NEW_SCORE2:
        game_enter_new_score(2);
        return;
    case EV_TRY_LEVEL:
        game.tryLevel = 1;
        start_new_game(cfg.tryLevel - 1, MODE_GAME, cfg.tryPlayer);
        return;
    }
}
