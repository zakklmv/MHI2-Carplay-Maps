/* Minimal libairplay stand-in for QEMU tests: CFLite subset + session entry points. */
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

enum { STR = 1, NUM, DICT, ARR };
typedef struct obj {
    int kind, rc;
    char *s;
    int64_t n;
    struct obj **k, **v;
    long len, cap;
} obj;

int kCFLDictionaryKeyCallBacksCFLTypes;
int kCFLDictionaryValueCallBacksCFLTypes;
int kCFLArrayCallBacksCFLTypes;

static obj *mk(int kind)
{
    obj *o = calloc(1, sizeof(obj));
    o->kind = kind;
    o->rc = 1;
    return o;
}

static void grow(obj *o)
{
    if (o->len == o->cap) {
        o->cap = o->cap ? o->cap * 2 : 4;
        o->k = realloc(o->k, o->cap * sizeof(obj *));
        o->v = realloc(o->v, o->cap * sizeof(obj *));
    }
}

void *CFStringCreateWithCString(void *a, const char *s, unsigned enc)
{
    obj *o = mk(STR);
    (void)a; (void)enc;
    o->s = strdup(s);
    return o;
}

void *CFRetain(void *o) { ((obj *)o)->rc++; return o; }
void CFRelease(void *o) { ((obj *)o)->rc--; }

void *CFDictionaryCreateMutable(void *a, long cap, const void *kcb, const void *vcb)
{
    (void)a; (void)cap;
    if (kcb != &kCFLDictionaryKeyCallBacksCFLTypes || vcb != &kCFLDictionaryValueCallBacksCFLTypes) {
        printf("MOCK-ERROR bad dict callbacks\n");
        return 0;
    }
    return mk(DICT);
}

static long find(obj *d, obj *k)
{
    long i;
    for (i = 0; i < d->len; i++)
        if (!strcmp(d->k[i]->s, k->s)) return i;
    return -1;
}

void CFDictionarySetValue(void *d0, const void *k0, const void *v0)
{
    obj *d = d0, *k = (obj *)k0, *v = (obj *)v0;
    long i = find(d, k);
    if (d->kind != DICT || k->kind != STR) { printf("MOCK-ERROR set on non-dict/key\n"); return; }
    if (i < 0) { grow(d); i = d->len++; d->k[i] = CFRetain(k); }
    d->v[i] = CFRetain(v);
}

void *CFDictionaryGetValue(void *d, const void *k)
{
    long i = find(d, (obj *)k);
    return i < 0 ? 0 : ((obj *)d)->v[i];
}

int CFDictionarySetInt64(void *d, const void *k, int64_t n)
{
    obj *v = mk(NUM);
    v->n = n;
    CFDictionarySetValue(d, k, v);
    return 0;
}

int64_t CFDictionaryGetInt64(void *d, const void *k, int *err)
{
    obj *v = CFDictionaryGetValue(d, k);
    if (!v || v->kind != NUM) { if (err) *err = -6727; return 0; }
    if (err) *err = 0;
    return v->n;
}

void *CFArrayCreateMutable(void *a, long cap, const void *cb)
{
    (void)a; (void)cap;
    if (cb != &kCFLArrayCallBacksCFLTypes) { printf("MOCK-ERROR bad array callbacks\n"); return 0; }
    return mk(ARR);
}

void CFArrayAppendValue(void *a, const void *v)
{
    obj *o = a;
    grow(o);
    o->v[o->len++] = CFRetain((void *)v);
}

long CFArrayGetCount(void *a) { return ((obj *)a)->len; }
uint32_t CFArrayGetTypeID(void) { return ARR; }

void *CFDictionaryGetTypedValue(const void *d, const void *k, uint32_t type, int32_t *err)
{
    obj *v = CFDictionaryGetValue((void *)d, k);
    if (!v || (uint32_t)v->kind != type) { if (err) *err = -6727; return 0; }
    if (err) *err = 0;
    return v;
}
void *CFArrayGetValueAtIndex(void *a, long i) { return ((obj *)a)->v[i]; }

void mock_dump(void *o0, int ind)
{
    obj *o = o0;
    long i;
    if (!o) { printf("null"); return; }
    switch (o->kind) {
    case STR: printf("\"%s\"", o->s); break;
    case NUM: printf("%lld", (long long)o->n); break;
    case ARR:
        printf("[");
        for (i = 0; i < o->len; i++) { if (i) printf(", "); mock_dump(o->v[i], ind); }
        printf("]");
        break;
    case DICT:
        printf("{");
        for (i = 0; i < o->len; i++) {
            printf("%s%s: ", i ? ", " : "", o->k[i]->s);
            mock_dump(o->v[i], ind);
        }
        printf("}");
        break;
    }
}

/* real (stock) entry points */
void *AirPlayCopyServerInfo(void *a, void *b, void *c, void *d)
{
    obj *info = CFDictionaryCreateMutable(0, 0, &kCFLDictionaryKeyCallBacksCFLTypes, &kCFLDictionaryValueCallBacksCFLTypes);
    obj *displays = CFArrayCreateMutable(0, 0, &kCFLArrayCallBacksCFLTypes);
    obj *main = CFDictionaryCreateMutable(0, 0, &kCFLDictionaryKeyCallBacksCFLTypes, &kCFLDictionaryValueCallBacksCFLTypes);
    (void)a; (void)b; (void)c; (void)d;
    CFDictionarySetInt64(main, CFStringCreateWithCString(0, "widthPixels", 0), 800);
    CFArrayAppendValue(displays, main);
    CFDictionarySetValue(info, CFStringCreateWithCString(0, "displays", 0), displays);
    printf("airplay: CopyServerInfo\n");
    return info;
}

int AirPlayReceiverSessionStart(void *s, void *b, void *c, void *d)
{
    (void)b; (void)c; (void)d;
    printf("airplay: SessionStart %p\n", s);
    return 0;
}

void AirPlayReceiverSessionTearDown(void *s, void *b, void *c, void *d)
{
    (void)b; (void)c; (void)d;
    printf("airplay: SessionTearDown %p\n", s);
}

struct pending { void (*done)(int, void *, void *); void *ctx; };

static void *complete_later(void *p0)
{
    struct pending *p = p0;
    usleep(30000);
    p->done(0, 0, p->ctx);
    free(p);
    return 0;
}

int AirPlayReceiverSessionSendCommand(void *s, void *cmd, void (*done)(int, void *, void *), void *ctx)
{
    pthread_t th;
    struct pending *p = malloc(sizeof(*p));
    printf("SENDCMD session=%p rc=%d ", s, ((obj *)s)->rc);
    mock_dump(cmd, 0);
    printf("\n");
    fflush(stdout);
    p->done = done;
    p->ctx = ctx;
    if (done) pthread_create(&th, 0, complete_later, p);
    pthread_detach(th);
    return 0;
}

void *mock_new_session(void) { return mk(DICT); }
