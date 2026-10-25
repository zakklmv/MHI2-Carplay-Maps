/* Stand-in for omonob core.so: interposes the same symbols and reaches libairplay via RTLD_NEXT. */
#include <dlfcn.h>
#include <stdio.h>

extern void *CFStringCreateWithCString(void *, const char *, unsigned);
extern void *CFDictionaryCreateMutable(void *, long, const void *, const void *);
extern void CFDictionarySetValue(void *, const void *, const void *);
extern int CFDictionarySetInt64(void *, const void *, long long);
extern void *CFDictionaryGetValue(void *, const void *);
extern void *CFArrayCreateMutable(void *, long, const void *);
extern void CFArrayAppendValue(void *, const void *);
extern int kCFLDictionaryKeyCallBacksCFLTypes, kCFLDictionaryValueCallBacksCFLTypes, kCFLArrayCallBacksCFLTypes;

#define S(x) CFStringCreateWithCString(0, x, 0)

static void *dict(void)
{
    return CFDictionaryCreateMutable(0, 0, &kCFLDictionaryKeyCallBacksCFLTypes, &kCFLDictionaryValueCallBacksCFLTypes);
}

static void *rect(int x, int y, int w, int h)
{
    void *d = dict();
    CFDictionarySetInt64(d, S("originXPixels"), x);
    CFDictionarySetInt64(d, S("originYPixels"), y);
    CFDictionarySetInt64(d, S("widthPixels"), w);
    CFDictionarySetInt64(d, S("heightPixels"), h);
    return d;
}

void *AirPlayCopyServerInfo(void *a, void *b, void *c, void *d)
{
    void *(*next)(void *, void *, void *, void *) = dlsym(RTLD_NEXT, "AirPlayCopyServerInfo");
    void *info = next(a, b, c, d);
    void *cl = dict(), *va = rect(0, 0, 1010, 376), *areas = CFArrayCreateMutable(0, 1, &kCFLArrayCallBacksCFLTypes);
    CFDictionarySetValue(va, S("safeArea"), rect(202, 16, 606, 344));
    CFArrayAppendValue(areas, va);
    CFDictionarySetValue(cl, S("uuid"), S("b7e6c5a0-2222-4000-8000-000000000002"));
    CFDictionarySetInt64(cl, S("type"), 111);
    CFDictionarySetInt64(cl, S("maxFPS"), 40);
    CFDictionarySetInt64(cl, S("widthPixels"), 1010);
    CFDictionarySetInt64(cl, S("heightPixels"), 376);
    CFDictionarySetInt64(cl, S("widthPhysical"), 202);
    CFDictionarySetInt64(cl, S("heightPhysical"), 75);
    CFDictionarySetValue(cl, S("initialURL"), S("maps:/car/instrumentcluster/map"));
    CFDictionarySetValue(cl, S("viewAreas"), areas);
    CFArrayAppendValue(CFDictionaryGetValue(info, S("displays")), cl);
    printf("core: appended cluster display\n");
    return info;
}

int AirPlayReceiverSessionStart(void *s, void *b, void *c, void *d)
{
    int (*next)(void *, void *, void *, void *) = dlsym(RTLD_NEXT, "AirPlayReceiverSessionStart");
    printf("core: SessionStart\n");
    return next(s, b, c, d);
}

void AirPlayReceiverSessionTearDown(void *s, void *b, void *c, void *d)
{
    void (*next)(void *, void *, void *, void *) = dlsym(RTLD_NEXT, "AirPlayReceiverSessionTearDown");
    printf("core: SessionTearDown\n");
    next(s, b, c, d);
}
