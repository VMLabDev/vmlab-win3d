/* Shared harness for the vmlab-win3d probes: command line, window, scene,
 * pixel readback bookkeeping and the JSON result. Each probe supplies only the
 * graphics API code. See probes/README.md for the CLI and JSON schema. */
#ifndef PROBE_H
#define PROBE_H

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define PROBE_SCHEMA_VERSION 1
#define PROBE_MAX_VERTICES 16384
#define PROBE_MARKERS 3

/* Exit codes, also recorded in the JSON. */
enum {
    PROBE_EXIT_PASS = 0,
    PROBE_EXIT_API_FAILURE = 1,
    PROBE_EXIT_READBACK_MISMATCH = 2,
    PROBE_EXIT_USAGE = 3,
};

/* Probe flags for probe_init. */
enum {
    PROBE_ALLOW_WARP = 1, /* accepts --warp */
};

/* A 2D vertex in normalised device coordinates (y up) with an RGBA colour. */
typedef struct probe_vertex {
    float x, y;
    float r, g, b, a;
} probe_vertex;

/* A fixed square of known colour that the readback checks. Pixel coordinates
 * have their origin at the top-left of the client area. */
typedef struct probe_marker {
    const char *name;
    int x, y; /* the pixel read back, the centre of the square */
    uint8_t rgb[3];
} probe_marker;

typedef struct probe_ctx {
    const char *probe;    /* "d3d11" */
    const char *api;      /* "Direct3D 11" */
    const char *title;    /* large on-screen text, "DIRECT3D 11" */
    const char *arch;     /* "x64" or "x86" */
    double seconds;       /* --seconds */
    int warp;             /* --warp */
    int width, height;    /* client size in pixels */
    wchar_t out_path[MAX_PATH];
    HWND hwnd;
    int closed;           /* the window was closed */
    LARGE_INTEGER freq, start;
    double end_time;
    long frames;
    probe_marker markers[PROBE_MARKERS];
    int readback_done;
    char subtitle[128];
} probe_ctx;

/* Parses the command line and creates and shows the window. Returns 0 to
 * continue, or an exit code (usage or help) to return from main at once. */
int probe_init(probe_ctx *ctx, int argc, wchar_t **argv, const char *probe,
               const char *api, const char *title, unsigned flags);

/* Pumps window messages. Returns 0 once the window has been closed. */
int probe_pump(probe_ctx *ctx);

/* Seconds since probe_init. */
double probe_now(const probe_ctx *ctx);

/* Whether this frame is the last one: --seconds have elapsed or the window
 * was closed. The last frame is the one that is read back. */
int probe_last_frame(const probe_ctx *ctx);

/* Counts a presented frame. */
void probe_frame_presented(probe_ctx *ctx);

/* Writes the scene for time t into v; returns the vertex count (a triangle
 * list). Sets the small line of text drawn under the title. */
int probe_build_scene(probe_ctx *ctx, double t, probe_vertex *v, int max);
void probe_set_subtitle(probe_ctx *ctx, const char *text);

/* The background clear colour, linear RGBA. */
extern const float probe_clear_color[4];

/* Records the colour read back for marker i (0..PROBE_MARKERS-1). */
void probe_readback(probe_ctx *ctx, int i, uint8_t r, uint8_t g, uint8_t b);

/* Reads every marker from a CPU copy of the frame. base points at pixel
 * (0,0), pitch is bytes per row, bgra selects B,G,R,A byte order instead of
 * R,G,B,A. bottom_up is set when row 0 is the bottom row (OpenGL). */
void probe_readback_image(probe_ctx *ctx, const uint8_t *base, size_t pitch,
                          int bgra, int bottom_up);

/* Records an error; any error makes the exit code PROBE_EXIT_API_FAILURE. */
void probe_error(probe_ctx *ctx, const char *fmt, ...);
/* Records an error for a failed HRESULT and returns 1 when hr failed. */
int probe_hr(probe_ctx *ctx, HRESULT hr, const char *what);

/* Key/value facts for the JSON "adapter" and "api" objects. Strings are UTF-8. */
typedef enum { PROBE_ADAPTER, PROBE_API } probe_section;
void probe_str(probe_ctx *ctx, probe_section s, const char *key, const char *value);
void probe_wstr(probe_ctx *ctx, probe_section s, const char *key, const wchar_t *value);
void probe_int(probe_ctx *ctx, probe_section s, const char *key, long long value);
void probe_hex(probe_ctx *ctx, probe_section s, const char *key, unsigned value);
void probe_bool(probe_ctx *ctx, probe_section s, const char *key, int value);
/* Appends a string to the JSON array named key in section s. */
void probe_list_add(probe_ctx *ctx, probe_section s, const char *key, const char *value);

/* Classifies an adapter or renderer as a software renderer by name and PCI
 * vendor (0x1414 is Microsoft). Records adapter.software and
 * adapter.software_reason. Pass vendor_id -1 when unknown. */
void probe_classify(probe_ctx *ctx, const char *name, long vendor_id, int force_software,
                    const char *force_reason);

/* The vendor name for a PCI vendor ID, or NULL. */
const char *probe_vendor_name(unsigned vendor_id);

/* Converts a wide string to UTF-8 in a static rotating buffer. */
const char *probe_utf8(const wchar_t *w);

/* Formats a 64-bit driver version (four 16-bit fields) as a.b.c.d. */
const char *probe_version4(unsigned long long v);

/* Writes the JSON result and returns the process exit code. */
int probe_finish(probe_ctx *ctx);

/* Loads d3dcompiler_47.dll and compiles HLSL. Returns a buffer the caller
 * frees with free(), or NULL after recording an error. */
void *probe_compile_hlsl(probe_ctx *ctx, const char *src, const char *entry,
                         const char *target, size_t *size);

#ifdef __cplusplus
}
#endif

#endif
