#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>
#include "plist.h"

typedef struct { const char *p, *end; } Rd;

static void skip_ws(Rd *r) {
    for (;;) {
        while (r->p < r->end && isspace((unsigned char)*r->p)) r->p++;
        if (r->p + 4 <= r->end && !strncmp(r->p, "<!--", 4)) {
            const char *e = strstr(r->p, "-->");
            r->p = e ? e + 3 : r->end;
            continue;
        }
        if (r->p + 2 <= r->end && (!strncmp(r->p, "<?", 2) || !strncmp(r->p, "<!", 2))) {
            while (r->p < r->end && *r->p != '>') r->p++;
            if (r->p < r->end) r->p++;
            continue;
        }
        break;
    }
}

/* reads a tag: returns 1 and fills name; *closing for </x>, *empty for <x/> */
static int read_tag(Rd *r, char *name, int max, int *closing, int *empty) {
    skip_ws(r);
    if (r->p >= r->end || *r->p != '<') return 0;
    r->p++;
    *closing = 0;
    *empty = 0;
    if (r->p < r->end && *r->p == '/') { *closing = 1; r->p++; }
    int n = 0;
    while (r->p < r->end && !isspace((unsigned char)*r->p) && *r->p != '>' && *r->p != '/') {
        if (n < max - 1) name[n++] = *r->p;
        r->p++;
    }
    name[n] = 0;
    while (r->p < r->end && *r->p != '>') {
        if (*r->p == '/') *empty = 1;
        r->p++;
    }
    if (r->p < r->end) r->p++;
    return 1;
}

static char *unescape(const char *s, int len) {
    char *o = malloc(len + 1);
    int k = 0;
    for (int i = 0; i < len; i++) {
        if (s[i] == '&') {
            const char *semi = memchr(s + i, ';', len - i);
            if (semi) {
                int n = semi - (s + i);
                if (!strncmp(s + i, "&lt;", 4)) o[k++] = '<';
                else if (!strncmp(s + i, "&gt;", 4)) o[k++] = '>';
                else if (!strncmp(s + i, "&amp;", 5)) o[k++] = '&';
                else if (!strncmp(s + i, "&quot;", 6)) o[k++] = '"';
                else if (!strncmp(s + i, "&apos;", 6)) o[k++] = '\'';
                else if (s[i + 1] == '#') {
                    long c = s[i + 2] == 'x' ? strtol(s + i + 3, NULL, 16) : strtol(s + i + 2, NULL, 10);
                    if (c < 0x80) o[k++] = (char)c;
                    else if (c < 0x800) { o[k++] = 0xC0 | (c >> 6); o[k++] = 0x80 | (c & 63); }
                    else { o[k++] = 0xE0 | (c >> 12); o[k++] = 0x80 | ((c >> 6) & 63); o[k++] = 0x80 | (c & 63); }
                } else { memcpy(o + k, s + i, n + 1); k += n + 1; }
                i += n;
                continue;
            }
        }
        o[k++] = s[i];
    }
    o[k] = 0;
    return o;
}

static char *read_text(Rd *r, const char *tag) {
    const char *start = r->p;
    char close[32];
    snprintf(close, sizeof close, "</%s>", tag);
    const char *e = start;
    int cl = strlen(close);
    while (e + cl <= r->end && strncmp(e, close, cl)) e++;
    if (e + cl > r->end) { r->p = r->end; return strdup(""); }
    char *s = unescape(start, e - start);
    r->p = e + cl;
    return s;
}

static PlNode *parse_value(Rd *r, const char *tag, int empty);

static PlNode *parse_next(Rd *r, int *closed) {
    char tag[32];
    int closing, empty;
    *closed = 0;
    if (!read_tag(r, tag, sizeof tag, &closing, &empty)) { *closed = 1; return NULL; }
    if (closing) { *closed = 1; return NULL; }
    return parse_value(r, tag, empty);
}

static PlNode *parse_value(Rd *r, const char *tag, int empty) {
    PlNode *n = calloc(1, sizeof(PlNode));
    if (!strcmp(tag, "dict")) {
        n->type = PL_DICT;
        if (empty) return n;
        PlNode **tail = &n->child;
        for (;;) {
            char t[32];
            int closing, emp;
            if (!read_tag(r, t, sizeof t, &closing, &emp) || closing) break;
            if (strcmp(t, "key")) { pl_free(parse_value(r, t, emp)); continue; }
            char *key = emp ? strdup("") : read_text(r, "key");
            int closed;
            PlNode *v = parse_next(r, &closed);
            if (!v) { free(key); break; }
            v->key = key;
            *tail = v;
            tail = &v->next;
        }
    } else if (!strcmp(tag, "array")) {
        n->type = PL_ARRAY;
        if (empty) return n;
        PlNode **tail = &n->child;
        for (;;) {
            int closed;
            PlNode *v = parse_next(r, &closed);
            if (!v) break;
            *tail = v;
            tail = &v->next;
        }
    } else if (!strcmp(tag, "string") || !strcmp(tag, "data") || !strcmp(tag, "date")) {
        n->type = PL_STRING;
        n->str = empty ? strdup("") : read_text(r, tag);
    } else if (!strcmp(tag, "integer")) {
        n->type = PL_INT;
        char *s = empty ? strdup("0") : read_text(r, tag);
        n->i = strtoll(s, NULL, 10);
        n->r = (double)n->i;
        free(s);
    } else if (!strcmp(tag, "real")) {
        n->type = PL_REAL;
        char *s = empty ? strdup("0") : read_text(r, tag);
        n->r = strtod(s, NULL);
        n->i = (long long)n->r;
        free(s);
    } else if (!strcmp(tag, "true") || !strcmp(tag, "false")) {
        n->type = PL_BOOL;
        n->i = tag[0] == 't';
        if (!empty) { char *s = read_text(r, tag); free(s); }
    } else if (!strcmp(tag, "plist")) {
        free(n);
        int closed;
        return parse_next(r, &closed);
    } else {
        n->type = PL_STRING;
        n->str = empty ? strdup("") : read_text(r, tag);
    }
    return n;
}

PlNode *pl_parse(const char *xml, int len) {
    Rd r = {xml, xml + len};
    if (len >= 6 && !memcmp(xml, "bplist", 6)) return NULL;
    int closed;
    return parse_next(&r, &closed);
}

void pl_free(PlNode *n) {
    while (n) {
        PlNode *next = n->next;
        pl_free(n->child);
        free(n->key);
        free(n->str);
        free(n);
        n = next;
    }
}

PlNode *pl_get(const PlNode *d, const char *key) {
    if (!d || d->type != PL_DICT) return NULL;
    for (PlNode *c = d->child; c; c = c->next)
        if (c->key && !strcmp(c->key, key)) return c;
    return NULL;
}

int pl_count(const PlNode *a) {
    int n = 0;
    if (a) for (PlNode *c = a->child; c; c = c->next) n++;
    return n;
}

PlNode *pl_at(const PlNode *a, int i) {
    if (!a) return NULL;
    for (PlNode *c = a->child; c; c = c->next)
        if (i-- == 0) return c;
    return NULL;
}

long long pl_int(const PlNode *n, long long def) {
    if (!n) return def;
    if (n->type == PL_INT || n->type == PL_BOOL) return n->i;
    if (n->type == PL_REAL) return (long long)n->r;
    if (n->type == PL_STRING && n->str) return strtoll(n->str, NULL, 10);   /* NSString intValue */
    return def;
}

double pl_real(const PlNode *n, double def) {
    if (!n) return def;
    if (n->type == PL_REAL) return n->r;
    if (n->type == PL_INT || n->type == PL_BOOL) return (double)n->i;
    if (n->type == PL_STRING && n->str) return strtod(n->str, NULL);
    return def;
}

const char *pl_str(const PlNode *n, const char *def) {
    if (!n || n->type != PL_STRING) return def;
    return n->str;
}

PlNode *pl_new(PlType t) { PlNode *n = calloc(1, sizeof(PlNode)); n->type = t; return n; }
PlNode *pl_new_int(long long v) { PlNode *n = pl_new(PL_INT); n->i = v; n->r = (double)v; return n; }
PlNode *pl_new_str(const char *s) { PlNode *n = pl_new(PL_STRING); n->str = strdup(s ? s : ""); return n; }

void pl_append(PlNode *a, PlNode *item) {
    PlNode **t = &a->child;
    while (*t) t = &(*t)->next;
    *t = item;
}

void pl_set(PlNode *d, const char *key, PlNode *v) {
    v->key = strdup(key);
    PlNode **t = &d->child;
    while (*t) {
        if ((*t)->key && !strcmp((*t)->key, key)) {
            v->next = (*t)->next;
            (*t)->next = NULL;
            pl_free(*t);
            *t = v;
            return;
        }
        t = &(*t)->next;
    }
    *t = v;
}

/* ------------------------------------------------------------------ writing */

typedef struct { char *b; int n, cap; } Wr;

static void put(Wr *w, const char *s) {
    int l = strlen(s);
    if (w->n + l + 1 > w->cap) {
        w->cap = (w->n + l + 1) * 2;
        w->b = realloc(w->b, w->cap);
    }
    memcpy(w->b + w->n, s, l + 1);
    w->n += l;
}

static void put_escaped(Wr *w, const char *s) {
    char c[2] = {0, 0};
    for (; *s; s++) {
        if (*s == '<') put(w, "&lt;");
        else if (*s == '>') put(w, "&gt;");
        else if (*s == '&') put(w, "&amp;");
        else { c[0] = *s; put(w, c); }
    }
}

static void indent(Wr *w, int d) { for (int i = 0; i < d; i++) put(w, "\t"); }

static int keycmp(const void *a, const void *b) {
    return strcmp((*(PlNode *const *)a)->key, (*(PlNode *const *)b)->key);
}

static void write_node(Wr *w, const PlNode *n, int d) {
    char tmp[64];
    switch (n->type) {
    case PL_DICT: {
        if (!n->child) { indent(w, d); put(w, "<dict/>\n"); break; }
        indent(w, d); put(w, "<dict>\n");
        /* keys in sorted order, like NSDictionary's writer */
        int cnt = pl_count(n);
        const PlNode **v = malloc(sizeof(PlNode *) * cnt);
        int k = 0;
        for (PlNode *c = n->child; c; c = c->next) v[k++] = c;
        qsort(v, cnt, sizeof(PlNode *), keycmp);
        for (int i = 0; i < cnt; i++) {
            indent(w, d + 1); put(w, "<key>"); put_escaped(w, v[i]->key); put(w, "</key>\n");
            write_node(w, v[i], d + 1);
        }
        free(v);
        indent(w, d); put(w, "</dict>\n");
        break;
    }
    case PL_ARRAY:
        if (!n->child) { indent(w, d); put(w, "<array/>\n"); break; }
        indent(w, d); put(w, "<array>\n");
        for (PlNode *c = n->child; c; c = c->next) write_node(w, c, d + 1);
        indent(w, d); put(w, "</array>\n");
        break;
    case PL_STRING:
        indent(w, d); put(w, "<string>"); put_escaped(w, n->str ? n->str : ""); put(w, "</string>\n");
        break;
    case PL_INT:
        indent(w, d); snprintf(tmp, sizeof tmp, "<integer>%lld</integer>\n", n->i); put(w, tmp);
        break;
    case PL_REAL:
        indent(w, d); snprintf(tmp, sizeof tmp, "<real>%.17g</real>\n", n->r); put(w, tmp);
        break;
    case PL_BOOL:
        indent(w, d); put(w, n->i ? "<true/>\n" : "<false/>\n");
        break;
    }
}

char *pl_write(const PlNode *root, int *len) {
    Wr w = {0};
    put(&w, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
            "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
            "<plist version=\"1.0\">\n");
    write_node(&w, root, 0);
    put(&w, "</plist>\n");
    if (len) *len = w.n;
    return w.b;
}
