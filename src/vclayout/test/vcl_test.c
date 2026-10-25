/* Drives the shim like dio_manager would: /info, session start, VC view changes, teardown. */
#include <stdio.h>
#include <unistd.h>

extern void *AirPlayCopyServerInfo(void *, void *, void *, void *);
extern int AirPlayReceiverSessionStart(void *, void *, void *, void *);
extern void AirPlayReceiverSessionTearDown(void *, void *, void *, void *);
extern void mock_dump(void *, int);
extern void *mock_new_session(void);
extern void *CFStringCreateWithCString(void *, const char *, unsigned);
extern void *CFDictionaryCreateMutable(void *, long, const void *, const void *);
extern void CFDictionarySetValue(void *, const void *, const void *);
extern void *CFArrayCreateMutable(void *, long, const void *);
extern void CFArrayAppendValue(void *, const void *);
extern int kCFLDictionaryKeyCallBacksCFLTypes, kCFLDictionaryValueCallBacksCFLTypes, kCFLArrayCallBacksCFLTypes;

static void view(int v)
{
    FILE *f = fopen("/ramdisk/cpmap-vclayout", "w");
    if (f) { fprintf(f, "%d\n", v); fclose(f); }
    printf("TEST view=%d\n", v);
    fflush(stdout);
}

int main(void)
{
    void *info, *sess = mock_new_session();
    FILE *f;

    /* debug marker absent at load: nothing may be logged */
    printf("TEST shim log before marker: %s\n",
           access("/ramdisk/cpmap-vclayout-shim.log", F_OK) == 0 ? "PRESENT (BAD)" : "absent (ok)");
    f = fopen("/ramdisk/cpmap-debug-active", "w");
    if (f) fclose(f);

    view(2);
    info = AirPlayCopyServerInfo(0, 0, 0, 0);
    printf("INFO ");
    mock_dump(info, 0);
    printf("\n");
    fflush(stdout);

    printf("TEST start rc=%d\n", AirPlayReceiverSessionStart(sess, 0, 0, 0));
    fflush(stdout);
    sleep(7);                 /* settle (5 s) + updateViewArea(2) + showUI */
    view(0); sleep(2);        /* classic: ETA url */
    view(3); sleep(2);        /* no map: nothing */
    {   /* stream-only teardown (as seen in the car ~16 s after start): session must be kept */
        void *req = CFDictionaryCreateMutable(0, 0, &kCFLDictionaryKeyCallBacksCFLTypes, &kCFLDictionaryValueCallBacksCFLTypes);
        void *streams = CFArrayCreateMutable(0, 1, &kCFLArrayCallBacksCFLTypes);
        CFArrayAppendValue(streams, CFStringCreateWithCString(0, "110", 0));
        CFDictionarySetValue(req, CFStringCreateWithCString(0, "streams", 0), streams);
        AirPlayReceiverSessionTearDown(sess, req, 0, 0);
        printf("TEST partial teardown done\n");
        fflush(stdout);
    }
    view(1); sleep(2);        /* large map: still switches */
    AirPlayReceiverSessionTearDown(sess, 0, 0, 0);   /* full teardown */
    view(0); sleep(2);        /* no session: nothing */
    printf("TEST done\n");
    return 0;
}
