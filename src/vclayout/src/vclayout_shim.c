/*
 * libcpmap-vclayout.so — per-Virtual-Cockpit-view CarPlay cluster geometry for MU1440 + 790.
 *
 * Preloaded in front of omonob's core.so (LD_PRELOAD=this:core.so:...). core.so itself resolves
 * the real libairplay symbols with dlsym(RTLD_NEXT), so the call chain is
 * libairplay -> this shim -> core.so -> libairplay.
 *
 *  - AirPlayCopyServerInfo: after core.so has appended its stream-111 cluster display, replace
 *    its single viewArea with one viewArea per VC view (same 1010x376 visible frame, different
 *    nested safeArea), set initialViewArea / adjacentViewAreas, physical size and initialURL.
 *  - Session Start/TearDown: remember the live session.
 *  - Worker thread: follows the VC view written by the Java tracker (/ramdisk/cpmap-vclayout,
 *    fallback /mnt/ota) and ships updateViewArea{uuid,viewAreaIndex,...} then showUI{uuid,url}
 *    through AirPlayReceiverSessionSendCommand. core.so already negotiates the "viewAreas"
 *    feature in its SETUP response (enabledFeatures = iAPChannel, viewAreas, altScreen).
 *
 * Command format and CFLite usage follow luka-dev/mib2q-carplay-rgi hook/altscreen
 * (GPL-3.0-or-later).
 */
#include <dlfcn.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define LOG_PATH       "/ramdisk/cpmap-vclayout-shim.log"
/* Logging only for a boot where dio_manager.wrapper.sh consumed the one-shot toolbox debug flag.
 * /ramdisk is 20 MB and holds the 16 MB video ring (omonob issue #48): log + .1 stay < 256 KB. */
#define DEBUG_MARKER   "/ramdisk/cpmap-debug-active"
#define LOG_MAX_BYTES  (128 * 1024)
#define RAM_LAYOUT     "/ramdisk/cpmap-vclayout"
#define OTA_LAYOUT     "/mnt/ota/cpmap-vclayout"
#define CLUSTER_UUID   "b7e6c5a0-2222-4000-8000-000000000002"
#define CFENC_UTF8     0x08000100u

#define VIEW_W         1010
#define VIEW_H         376
/* ~67% UI scale vs core.so's 202x75. CarPlay layout pitch scale = 0.153 mm / (PHYS_W_MM / VIEW_W)
 * = 0.515 -> a safe area at least 660 * 0.515 = ~340 px wide gets the edge-column card layout. */
#define PHYS_W_MM      300
#define PHYS_H_MM      112

#define SETTLE_MS      5000     /* let core.so's own activation/showUI happen first */
#define POLL_US        250000
#define INFLIGHT_MS    5000
#define RETRY_MS       2000

struct area {
    int x, y, w, h;
    const char *url;
    const char *name;
};

/* index == viewArea index advertised to the phone */
static const struct area AREAS[] = {
    /* map only + ETA: road name on top, ETA pill at the bottom, no turn card */
    {364, 56, 280, 280, "maps:/car/instrumentcluster/map",             "classic"},
    { 40, 56, 930, 222, "maps:/car/instrumentcluster",                 "large-map"},
    /* narrow (< ~340 px at this scale) => always the centred card layout, card <= 320 px */
    {345, 56, 320, 274, "maps:/car/instrumentcluster",                 "map-panels"},
};
#define NAREAS ((int)(sizeof(AREAS) / sizeof(AREAS[0])))

/* VC view (0..4) -> viewArea index; views 3 and 4 show no map: keep the current area */
static int area_for_view(int view)
{
    return (view >= 0 && view < NAREAS) ? view : -1;
}

/* ── logging ─────────────────────────────────────────────────────────────── */

static pthread_mutex_t log_lock = PTHREAD_MUTEX_INITIALIZER;

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)(ts.tv_nsec / 1000000);
}

static void vcl_log(const char *fmt, ...)
{
    struct stat st;
    va_list ap;
    FILE *f;

    if (access(DEBUG_MARKER, F_OK) != 0)
        return;
    pthread_mutex_lock(&log_lock);
    if (stat(LOG_PATH, &st) == 0 && st.st_size > LOG_MAX_BYTES)
        rename(LOG_PATH, LOG_PATH ".1");
    f = fopen(LOG_PATH, "a");
    if (f) {
        fprintf(f, "%llu ", (unsigned long long)now_ms());
        va_start(ap, fmt);
        vfprintf(f, fmt, ap);
        va_end(ap);
        fputc('\n', f);
        fclose(f);
    }
    pthread_mutex_unlock(&log_lock);
}

/* ── libairplay CFLite (resolved lazily) ─────────────────────────────────── */

typedef void   *(*cf_str_t)(void *, const char *, unsigned);
typedef void   *(*cf_arrm_t)(void *, long, const void *);
typedef void    (*cf_arrapp_t)(void *, const void *);
typedef long    (*cf_arrcount_t)(void *);
typedef void   *(*cf_arrget_t)(void *, long);
typedef void   *(*cf_retain_t)(void *);
typedef void    (*cf_release_t)(void *);
typedef void    (*cf_dictset_t)(void *, const void *, const void *);
typedef void   *(*cf_dictget_t)(void *, const void *);
typedef void   *(*cf_dictm_t)(void *, long, const void *, const void *);
typedef int     (*cf_dictseti_t)(void *, const void *, int64_t);
typedef int64_t (*cf_dictgeti_t)(void *, const void *, int *);
typedef void   *(*cf_dictgettyped_t)(const void *, const void *, uint32_t, int32_t *);
typedef uint32_t (*cf_typeid_t)(void);
typedef void    (*completion_t)(int status, void *resp, void *ctx);
typedef int     (*sendcmd_t)(void *session, void *cmd, completion_t done, void *ctx);

static cf_str_t      cf_str;
static cf_arrm_t     cf_arrm;
static cf_arrapp_t   cf_arrapp;
static cf_arrcount_t cf_arrcount;
static cf_arrget_t   cf_arrget;
static cf_retain_t   cf_retain;
static cf_release_t  cf_release;
static cf_dictset_t  cf_dictset;
static cf_dictget_t  cf_dictget;
static cf_dictm_t    cf_dictm;
static cf_dictseti_t cf_dictseti;
static cf_dictgeti_t cf_dictgeti;
static sendcmd_t     sess_sendcmd;
static cf_dictgettyped_t cf_dictgettyped;
static cf_typeid_t   cf_array_typeid;
static const void   *arr_cb, *dict_keycb, *dict_valcb;
static int           cf_state = -1;
static pthread_mutex_t cf_lock = PTHREAD_MUTEX_INITIALIZER;

static int cf_ready(void)
{
    static const char *arr_cb_names[] = {
        "kCFTypeArrayCallBacks", "kCFLArrayCallBacksCFLTypes", "kCFLTypeArrayCallBacks", 0
    };
    int i;

    pthread_mutex_lock(&cf_lock);
    if (cf_state != 1) {
        cf_str      = (cf_str_t)     dlsym(RTLD_DEFAULT, "CFStringCreateWithCString");
        cf_arrm     = (cf_arrm_t)    dlsym(RTLD_DEFAULT, "CFArrayCreateMutable");
        cf_arrapp   = (cf_arrapp_t)  dlsym(RTLD_DEFAULT, "CFArrayAppendValue");
        cf_arrcount = (cf_arrcount_t)dlsym(RTLD_DEFAULT, "CFArrayGetCount");
        cf_arrget   = (cf_arrget_t)  dlsym(RTLD_DEFAULT, "CFArrayGetValueAtIndex");
        cf_retain   = (cf_retain_t)  dlsym(RTLD_DEFAULT, "CFRetain");
        cf_release  = (cf_release_t) dlsym(RTLD_DEFAULT, "CFRelease");
        cf_dictset  = (cf_dictset_t) dlsym(RTLD_DEFAULT, "CFDictionarySetValue");
        cf_dictget  = (cf_dictget_t) dlsym(RTLD_DEFAULT, "CFDictionaryGetValue");
        cf_dictm    = (cf_dictm_t)   dlsym(RTLD_DEFAULT, "CFDictionaryCreateMutable");
        cf_dictseti = (cf_dictseti_t)dlsym(RTLD_DEFAULT, "CFDictionarySetInt64");
        cf_dictgeti = (cf_dictgeti_t)dlsym(RTLD_DEFAULT, "CFDictionaryGetInt64");
        sess_sendcmd = (sendcmd_t)   dlsym(RTLD_DEFAULT, "AirPlayReceiverSessionSendCommand");
        cf_dictgettyped = (cf_dictgettyped_t)dlsym(RTLD_DEFAULT, "CFDictionaryGetTypedValue");
        cf_array_typeid = (cf_typeid_t)dlsym(RTLD_DEFAULT, "CFArrayGetTypeID");
        dict_keycb  = dlsym(RTLD_DEFAULT, "kCFLDictionaryKeyCallBacksCFLTypes");
        dict_valcb  = dlsym(RTLD_DEFAULT, "kCFLDictionaryValueCallBacksCFLTypes");
        arr_cb = 0;
        for (i = 0; arr_cb_names[i] && !arr_cb; i++)
            arr_cb = dlsym(RTLD_DEFAULT, arr_cb_names[i]);
        cf_state = (cf_str && cf_arrm && cf_arrapp && cf_arrcount && cf_arrget && cf_retain &&
                    cf_release && cf_dictset && cf_dictget && cf_dictm && cf_dictseti &&
                    cf_dictgeti && sess_sendcmd && arr_cb && dict_keycb && dict_valcb) ? 1 : 0;
        vcl_log("cf_ready=%d sendcmd=%p arr_cb=%p keycb=%p valcb=%p typedget=%p", cf_state,
             (void *)sess_sendcmd, arr_cb, dict_keycb, dict_valcb, (void *)cf_dictgettyped);
    }
    i = cf_state;
    pthread_mutex_unlock(&cf_lock);
    return i == 1;
}

static void rel(void *o)
{
    if (o) cf_release(o);
}

static int set_i64(void *d, const char *key, int64_t v)
{
    void *k = cf_str(0, key, CFENC_UTF8);
    int rc = k ? cf_dictseti(d, k, v) : -1;
    rel(k);
    return rc;
}

static int set_obj(void *d, const char *key, void *v)
{
    void *k = cf_str(0, key, CFENC_UTF8);
    if (!k || !v) {
        rel(k);
        return -1;
    }
    cf_dictset(d, k, v);
    rel(k);
    return 0;
}

static int set_str(void *d, const char *key, const char *val)
{
    void *v = cf_str(0, val, CFENC_UTF8);
    int rc = set_obj(d, key, v);
    rel(v);
    return rc;
}

static int64_t get_i64(void *d, const char *key, int *ok)
{
    void *k = cf_str(0, key, CFENC_UTF8);
    int err = -1;
    int64_t v = k ? cf_dictgeti(d, k, &err) : 0;
    rel(k);
    *ok = (k && err == 0);
    return v;
}

static void *new_dict(void)
{
    return cf_dictm(0, 0, dict_keycb, dict_valcb);
}

static void *new_rect(int x, int y, int w, int h)
{
    void *d = new_dict();
    if (d && (set_i64(d, "originXPixels", x) || set_i64(d, "originYPixels", y) ||
              set_i64(d, "widthPixels", w) || set_i64(d, "heightPixels", h))) {
        rel(d);
        d = 0;
    }
    return d;
}

static void *new_view_area(const struct area *a)
{
    void *va = new_rect(0, 0, VIEW_W, VIEW_H);
    void *safe = new_rect(a->x, a->y, a->w, a->h);
    if (!va || !safe || set_obj(va, "safeArea", safe)) {
        rel(va);
        va = 0;
    }
    rel(safe);
    return va;
}

/* ── shared state ─────────────────────────────────────────────────────────── */

static pthread_mutex_t st_lock = PTHREAD_MUTEX_INITIALIZER;
static void     *g_session;          /* not retained; cleared on TearDown */
static unsigned  g_gen;
static uint64_t  g_session_ms;
static int       g_declared;         /* our viewAreas are in the advertised display */
static int       g_initial = 0;
static int       g_applied = -1;
static int       g_inflight;
static uint64_t  g_inflight_ms;
static int       g_pending_ui = -1;
static uint64_t  g_next_try_ms;

static int read_view_file(const char *path)
{
    char c = 0;
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (fread(&c, 1, 1, f) != 1) c = 0;
    fclose(f);
    return (c >= '0' && c <= '4') ? c - '0' : -1;
}

static int read_view(void)
{
    int v = read_view_file(RAM_LAYOUT);
    return v >= 0 ? v : read_view_file(OTA_LAYOUT);
}

/* ── display advert ───────────────────────────────────────────────────────── */

static void patch_server_info(void *info)
{
    void *k, *displays, *cluster = 0, *areas = 0, *adjacent = 0;
    long n, i;
    int ok, view, idx;

    if (!cf_ready()) {
        vcl_log("server-info: CF unresolved, unchanged");
        return;
    }
    k = cf_str(0, "displays", CFENC_UTF8);
    displays = k ? cf_dictget(info, k) : 0;
    rel(k);
    if (!displays) {
        vcl_log("server-info: no displays array, unchanged");
        return;
    }
    n = cf_arrcount(displays);
    for (i = 0; i < n && !cluster; i++) {
        void *d = cf_arrget(displays, i);
        if (d && get_i64(d, "type", &ok) == 111 && ok)
            cluster = d;
    }
    if (!cluster) {
        vcl_log("server-info: no type-111 display among %ld, unchanged", n);
        return;
    }
    if (get_i64(cluster, "widthPixels", &ok) != VIEW_W || !ok ||
        get_i64(cluster, "heightPixels", &ok) != VIEW_H || !ok) {
        vcl_log("server-info: cluster is not %dx%d (not a 790 profile), unchanged", VIEW_W, VIEW_H);
        return;
    }

    view = read_view();
    idx = area_for_view(view);
    if (idx < 0) idx = 0;

    areas = cf_arrm(0, NAREAS, arr_cb);
    adjacent = cf_arrm(0, 0, arr_cb);
    if (!areas || !adjacent) goto out;
    for (i = 0; i < NAREAS; i++) {
        void *va = new_view_area(&AREAS[i]);
        if (!va) goto out;
        cf_arrapp(areas, va);
        rel(va);
    }
    if (set_obj(cluster, "viewAreas", areas) || set_i64(cluster, "initialViewArea", idx) ||
        set_obj(cluster, "adjacentViewAreas", adjacent) ||
        set_i64(cluster, "widthPhysical", PHYS_W_MM) || set_i64(cluster, "heightPhysical", PHYS_H_MM) ||
        set_str(cluster, "initialURL", AREAS[idx].url))
        goto out;

    pthread_mutex_lock(&st_lock);
    g_declared = 1;
    g_initial = idx;
    pthread_mutex_unlock(&st_lock);
    vcl_log("server-info: declared %d viewAreas, view=%d initialViewArea=%d (%s) phys=%dx%dmm url=%s",
         NAREAS, view, idx, AREAS[idx].name, PHYS_W_MM, PHYS_H_MM, AREAS[idx].url);
    for (i = 0; i < NAREAS; i++)
        vcl_log("  viewArea[%ld] %s safe=(%d,%d %dx%d) url=%s", i, AREAS[i].name,
             AREAS[i].x, AREAS[i].y, AREAS[i].w, AREAS[i].h, AREAS[i].url);
out:
    rel(areas);
    rel(adjacent);
}

/* ── session commands ─────────────────────────────────────────────────────── */

static void *retain_session(unsigned gen)
{
    void *s = 0;
    pthread_mutex_lock(&st_lock);
    if (g_session && g_gen == gen)
        s = cf_retain(g_session);
    pthread_mutex_unlock(&st_lock);
    return s;
}

static int ship(unsigned gen, const char *type, void *params, completion_t done, void *ctx)
{
    int rc = -6728;
    void *cmd = new_dict();
    void *sess;

    if (cmd && !set_str(cmd, "type", type) && !set_obj(cmd, "params", params)) {
        sess = retain_session(gen);
        if (sess) {
            rc = sess_sendcmd(sess, cmd, done, ctx);
            cf_release(sess);
        } else {
            rc = -6735;
        }
    }
    rel(cmd);
    return rc;
}

static void view_area_done(int status, void *resp, void *ctx)
{
    unsigned gen = (unsigned)((uintptr_t)ctx >> 2);
    int idx = (int)((uintptr_t)ctx & 3u);
    (void)resp;
    vcl_log("updateViewArea ack index=%d (%s) status=%d gen=%u", idx, AREAS[idx].name, status, gen);
    pthread_mutex_lock(&st_lock);
    if (gen == g_gen) {
        g_inflight = 0;
        g_applied = idx;          /* also on failure: no retry storm; showUI still sets the URL */
        g_pending_ui = idx;
    }
    pthread_mutex_unlock(&st_lock);
}

static void show_ui_done(int status, void *resp, void *ctx)
{
    (void)resp;
    vcl_log("showUI ack index=%d status=%d", (int)(uintptr_t)ctx, status);
}

static int send_view_area(unsigned gen, int idx)
{
    int rc = -6728;
    void *params = new_dict();
    void *adjacent = cf_arrm(0, 0, arr_cb);

    if (params && adjacent && !set_str(params, "uuid", CLUSTER_UUID) &&
        !set_i64(params, "viewAreaIndex", idx) && !set_i64(params, "animationDurationMillis", 0) &&
        !set_obj(params, "adjacentViewAreas", adjacent))
        rc = ship(gen, "updateViewArea", params, view_area_done,
                  (void *)(uintptr_t)((gen << 2) | (unsigned)idx));
    rel(adjacent);
    rel(params);
    vcl_log("updateViewArea ship index=%d (%s) rc=%d gen=%u", idx, AREAS[idx].name, rc, gen);
    return rc;
}

static int send_show_ui(unsigned gen, int idx)
{
    int rc = -6728;
    void *params = new_dict();

    if (params && !set_str(params, "uuid", CLUSTER_UUID) && !set_str(params, "url", AREAS[idx].url))
        rc = ship(gen, "showUI", params, show_ui_done, (void *)(uintptr_t)idx);
    rel(params);
    vcl_log("showUI ship index=%d url=%s rc=%d gen=%u", idx, AREAS[idx].url, rc, gen);
    return rc;
}

static void *worker(void *arg)
{
    int last_view = -2;
    (void)arg;
    vcl_log("worker started");
    for (;;) {
        int view, want, rc, pending, applied, inflight, declared, initial;
        unsigned gen;
        uint64_t t, start, inflight_ms, next_try;
        void *sess;

        usleep(POLL_US);
        view = read_view();
        if (view != last_view) {
            vcl_log("vc view=%d", view);
            last_view = view;
        }
        t = now_ms();

        pthread_mutex_lock(&st_lock);
        sess = g_session;
        gen = g_gen;
        start = g_session_ms;
        declared = g_declared;
        initial = g_initial;
        applied = g_applied;
        pending = g_pending_ui;
        inflight = g_inflight;
        inflight_ms = g_inflight_ms;
        next_try = g_next_try_ms;
        if (inflight && t - inflight_ms > INFLIGHT_MS) {
            g_inflight = inflight = 0;
            vcl_log("updateViewArea ack timeout, will retry");
        }
        pthread_mutex_unlock(&st_lock);

        if (!sess || !declared || t - start < SETTLE_MS || inflight || t < next_try)
            continue;

        if (pending >= 0) {
            send_show_ui(gen, pending);
            pthread_mutex_lock(&st_lock);
            if (gen == g_gen && g_pending_ui == pending) g_pending_ui = -1;
            pthread_mutex_unlock(&st_lock);
            continue;
        }

        want = area_for_view(view);
        if (want < 0) want = applied >= 0 ? applied : initial;
        if (want == applied) continue;

        pthread_mutex_lock(&st_lock);
        g_inflight = 1;
        g_inflight_ms = t;
        pthread_mutex_unlock(&st_lock);
        rc = send_view_area(gen, want);
        if (rc != 0) {
            pthread_mutex_lock(&st_lock);
            g_inflight = 0;
            g_next_try_ms = t + RETRY_MS;
            pthread_mutex_unlock(&st_lock);
        }
    }
    return 0;
}

static pthread_once_t worker_once = PTHREAD_ONCE_INIT;

static void start_worker(void)
{
    pthread_t th;
    pthread_attr_t a;
    pthread_attr_init(&a);
    pthread_attr_setdetachstate(&a, PTHREAD_CREATE_DETACHED);
    if (pthread_create(&th, &a, worker, 0) != 0)
        vcl_log("worker start failed");
    pthread_attr_destroy(&a);
}

/* ── interposed libairplay entry points (all args passed through) ─────────── */

typedef void *(*copy_info_t)(void *, void *, void *, void *);
typedef int   (*start_t)(void *, void *, void *, void *);
typedef void  (*teardown_t)(void *, void *, void *, void *);

void *AirPlayCopyServerInfo(void *a, void *b, void *c, void *d)
{
    static copy_info_t next;
    void *info;
    if (!next) next = (copy_info_t)dlsym(RTLD_NEXT, "AirPlayCopyServerInfo");
    if (!next) {
        vcl_log("AirPlayCopyServerInfo: next not found");
        return 0;
    }
    info = next(a, b, c, d);
    if (info) patch_server_info(info);
    return info;
}

int AirPlayReceiverSessionStart(void *s, void *b, void *c, void *d)
{
    static start_t next;
    int rc;
    if (!next) next = (start_t)dlsym(RTLD_NEXT, "AirPlayReceiverSessionStart");
    if (!next) {
        vcl_log("AirPlayReceiverSessionStart: next not found");
        return -6735;
    }
    rc = next(s, b, c, d);
    if (rc == 0) {
        pthread_mutex_lock(&st_lock);
        g_session = s;
        g_gen++;
        g_session_ms = now_ms();
        g_applied = -1;
        g_inflight = 0;
        g_pending_ui = -1;
        g_next_try_ms = 0;
        pthread_mutex_unlock(&st_lock);
        pthread_once(&worker_once, start_worker);
    }
    vcl_log("session start %p rc=%d gen=%u", s, rc, g_gen);
    return rc;
}

/* Stock treats a NULL request, a missing "streams" key or an empty "streams" array as a full
 * session teardown; a non-empty list only closes those streams and the session (and the cluster
 * stream) continues. Same rule as luka-dev/mib2q-carplay-rgi hook/framework/airplay_seams.c.
 * Falls back to an untyped lookup ("streams" is always an array in the protocol); if CF is
 * unavailable the teardown is treated as full, so no stale session pointer is ever kept. */
static int teardown_is_full(void *request)
{
    void *k, *streams;
    int full;
    if (!request) return 1;
    if (!cf_ready()) return 1;
    k = cf_str(0, "streams", CFENC_UTF8);
    if (!k) return 1;
    if (cf_dictgettyped && cf_array_typeid)
        streams = cf_dictgettyped(request, k, cf_array_typeid(), 0);
    else
        streams = cf_dictget(request, k);
    full = !streams || cf_arrcount(streams) == 0;
    rel(k);
    return full;
}

void AirPlayReceiverSessionTearDown(void *s, void *b, void *c, void *d)
{
    static teardown_t next;
    int full = teardown_is_full(b);
    vcl_log("session teardown %p %s", s, full ? "full" : "streams-only (session kept)");
    pthread_mutex_lock(&st_lock);
    if (full && g_session == s) {
        g_session = 0;
        g_gen++;
        g_applied = -1;
        g_inflight = 0;
        g_pending_ui = -1;
    }
    pthread_mutex_unlock(&st_lock);
    if (!next) next = (teardown_t)dlsym(RTLD_NEXT, "AirPlayReceiverSessionTearDown");
    if (next) next(s, b, c, d);
}

__attribute__((constructor))
static void vclayout_loaded(void)
{
    vcl_log("libcpmap-vclayout loaded pid=%d", (int)getpid());
}
