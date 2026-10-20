#ifndef PLIST_H
#define PLIST_H

/* XML property lists, as far as the level files need them */
typedef enum { PL_DICT, PL_ARRAY, PL_STRING, PL_INT, PL_REAL, PL_BOOL } PlType;

typedef struct PlNode {
    PlType type;
    char *key;              /* the key when the node is a dict entry */
    char *str;
    long long i;
    double r;
    struct PlNode *child, *next;
} PlNode;

PlNode *pl_parse(const char *xml, int len);
void pl_free(PlNode *n);
char *pl_write(const PlNode *root, int *len);   /* malloc'ed XML */

PlNode *pl_get(const PlNode *dict, const char *key);
int pl_count(const PlNode *arr);
PlNode *pl_at(const PlNode *arr, int i);
long long pl_int(const PlNode *n, long long def);
double pl_real(const PlNode *n, double def);
const char *pl_str(const PlNode *n, const char *def);

PlNode *pl_new(PlType t);
PlNode *pl_new_int(long long v);
PlNode *pl_new_str(const char *s);
void pl_append(PlNode *arr, PlNode *item);
void pl_set(PlNode *dict, const char *key, PlNode *value);   /* replaces an existing key */

#endif
