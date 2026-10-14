/* gltest — OpenGL through opengl32.dll (on NovaOS: Mesa's virgl on a 3D
 * virtio-gpu, Mesa's llvmpipe otherwise): pixel format, context, clear,
 * immediate mode, GLSL shaders, read-back, then a few seconds of animated
 * frames presented with SwapBuffers.
 * Build: x86_64-w64-mingw32-gcc -O2 -o gltest.exe gltest.c -lopengl32 -lgdi32 -luser32 -lm
 *        (i686-w64-mingw32-gcc for the 32-bit one).  Usage: gltest [seconds [DRIVER]]
 *        (DRIVER, virgl or llvmpipe: run with GALLIUM_DRIVER=DRIVER, and
 *        GL_RENDERER must name it)
 *
 * gltest fps [seconds]: the frame-rate test.  A scene that keeps the
 * rasterizer busy (64 blended full-window quads at 640x480) runs on virgl
 * (the host's GPU through the virtio-gpu) twice, its frames shown straight
 * from the host (direct presentation) and then read back and drawn through
 * the GDI (NOVA_PRESENT=gdi), and once on llvmpipe (NovaOS's CPU), each in
 * a child process whose GALLIUM_DRIVER names the driver; virgl must draw
 * more frames per second than llvmpipe, and direct presentation more than
 * the GDI way while moving fewer bytes through guest memory per frame (the
 * kernel's counters: NtNovaGpuCtl's statistics).  Under KVM, where the
 * host's drawing is nearly the whole frame, direct presentation must move
 * a sixteenth of the bytes or less instead of being faster.
 *
 * gltest present: direct presentation on virgl.  Four coloured quadrants
 * (red, green / blue, white) presented with SwapBuffers in a 256x192
 * window: shown from the host while the window is alone on screen
 * ("present: quadrants at X Y W H": the self-test checks the screen
 * there), through the GDI while a window covers part of it, and from the
 * host again once that window has gone.
 *
 * gltest monitors: direct presentation on the second monitor and at scale
 * 2.  The same quadrants on the second monitor ("present: display 2
 * quadrants at X Y W H", that monitor's own pixels), again once it is in
 * 2560x1600, where the desktop's scale is 2 and the screen shows each of
 * the window's pixels twice as wide and high ("display 2 at scale 2"),
 * and on the primary, now shown at half the desktop's scale ("display 1
 * at scale 2"); most frames must be shown from the host each time.
 *
 * gltest colors [DRIVER]: frames in each colour depth Mesa offers a window (8:8:8,
 * 5:6:5, 5:5:5:1, 4:4:4:4 and 10:10:10:2): four coloured bars presented
 * with SwapBuffers and read back from the window with GetPixel.  Mesa
 * presents the shallow ones as 16-bit DIBs with BI_BITFIELDS masks, which
 * gdi32 must honour (SDL2 asks for 3:3:2 and gets 4:4:4:4: Chocolate
 * Doom's colours came out wrong). */
#include <windows.h>
#include <GL/gl.h>
#include <GL/glext.h>
#include <stdio.h>
#include <math.h>
#include <string.h>
#include <cpuid.h>

static int pass, fail;
static void check(const char *what, int ok) { if (ok) pass++; else { fail++; printf("FAIL %s\n", what); } }

static LRESULT CALLBACK proc(HWND h, UINT m, WPARAM w, LPARAM l)
{
    if (m == WM_CLOSE) { PostQuitMessage(0); return 0; }
    return DefWindowProcA(h, m, w, l);
}

static int px_near(const unsigned char *p, int r, int g, int b)
{
    return abs(p[0] - r) < 8 && abs(p[1] - g) < 8 && abs(p[2] - b) < 8;
}

#define GLF(t, n) static t n
GLF(PFNGLCREATESHADERPROC, pCreateShader); GLF(PFNGLSHADERSOURCEPROC, pShaderSource);
GLF(PFNGLCOMPILESHADERPROC, pCompileShader); GLF(PFNGLGETSHADERIVPROC, pGetShaderiv);
GLF(PFNGLCREATEPROGRAMPROC, pCreateProgram); GLF(PFNGLATTACHSHADERPROC, pAttachShader);
GLF(PFNGLLINKPROGRAMPROC, pLinkProgram); GLF(PFNGLGETPROGRAMIVPROC, pGetProgramiv);
GLF(PFNGLUSEPROGRAMPROC, pUseProgram); GLF(PFNGLGETUNIFORMLOCATIONPROC, pGetUniformLocation);
GLF(PFNGLUNIFORM1FPROC, pUniform1f); GLF(PFNGLGETSHADERINFOLOGPROC, pGetShaderInfoLog);

static GLuint shader(GLenum kind, const char *src)
{
    GLuint s = pCreateShader(kind);
    pShaderSource(s, 1, &src, NULL);
    pCompileShader(s);
    GLint ok = 0;
    pGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) { char log[512]; pGetShaderInfoLog(s, sizeof(log), NULL, log); printf("shader: %s\n", log); }
    return s;
}

/* The kernel's display counters (NtNovaGpuCtl 14): bytes moved through
 * guest memory to show frames (sent to a virtio GPU's outputs, read back
 * by virgl, window bitmaps drawn by the desktop, its back buffer copied to
 * the screen), frames shown by direct presentation, the bytes the host
 * wrote into the screen for them, and the microseconds programs spent
 * asking for them (and waiting for the desktop meanwhile); false if none */
enum { ST_TO_HOST, ST_FROM_HOST, ST_PAINTED, ST_SCREEN, ST_DIRECT, ST_DIRECT_BYTES, ST_DIRECT_US, ST_DIRECT_LOCK_US,
       ST_N };
typedef LONG_PTR(NTAPI *PFN_NtNovaGpuCtl)(INT_PTR, ULONG, ULONG_PTR, void *);
static int gpu_stats(unsigned long long st[ST_N])
{
    static PFN_NtNovaGpuCtl ctl;
    memset(st, 0, sizeof(unsigned long long) * ST_N);
    if (!ctl) ctl = (PFN_NtNovaGpuCtl)GetProcAddress(GetModuleHandleA("ntdll.dll"), "NtNovaGpuCtl");
    return ctl && ctl(0, 14, 0, st) == 0;
}

/* The pointer out of the way (over a window it stops direct presentation) */
static void pointer_away(void) { SetCursorPos(GetSystemMetrics(SM_CXSCREEN) - 1, 0); }

/* ---- the frame-rate test ------------------------------------------------ */
#define FPS_W 640
#define FPS_H 480
#define FPS_QUADS 64

/* A window with an OpenGL context current on it, @w x @h inside; NULL if not */
static HWND gl_window(const char *title, int x, int y, int w, int h, HDC *dc, HGLRC *rc)
{
    RECT r = { 0, 0, w, h };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    HWND win = CreateWindowA("gltest", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, x, y, r.right - r.left,
                             r.bottom - r.top, 0, 0, GetModuleHandleA(NULL), 0);
    *dc = GetDC(win);
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
    int fmt = ChoosePixelFormat(*dc, &pfd);
    if (fmt <= 0 || !SetPixelFormat(*dc, fmt, &pfd)) return NULL;
    *rc = wglCreateContext(*dc);
    if (!*rc || !wglMakeCurrent(*dc, *rc)) return NULL;
    return win;
}

static void pump(void)
{
    MSG m;
    while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) DispatchMessageA(&m);
}

/* The child: draw the scene for @secs seconds; prints "fps-result
 * RENDERER|FRAMES|MS|DIRECT|BYTES|SWAP|ASK|LOCK": DIRECT of the frames
 * shown from the host, BYTES moved through guest memory meanwhile, and
 * microseconds in SwapBuffers, in the kernel's direct presentation and
 * waiting there for the desktop */
static int fps_child(int secs)
{
    HDC dc;
    HGLRC rc;
    if (!gl_window("OpenGL frame rate", 60, 60, FPS_W, FPS_H, &dc, &rc)) { printf("FAIL context\n"); return 1; }
    pointer_away();
    const char *renderer = (const char *)glGetString(GL_RENDERER);
    unsigned long long s0[ST_N], s1[ST_N];
    gpu_stats(s0);
    glViewport(0, 0, FPS_W, FPS_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, FPS_W, FPS_H, 0, -1, 1);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    LARGE_INTEGER qf;
    QueryPerformanceFrequency(&qf);
    long long swap = 0;
    DWORD t0 = GetTickCount(), frames = 0;
    while (GetTickCount() - t0 < (DWORD)secs * 1000) {
        pump();
        glClearColor(0.06f, 0.06f, ((frames * 4) & 0xFF) / 255.0f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_QUADS);
        for (int i = 0; i < FPS_QUADS; i++) {
            float x0 = (float)(i % 8), y0 = (float)(i / 8 % 8);
            glColor4ub((GLubyte)(40 + i * 3), (GLubyte)(255 - i * 3), (GLubyte)(128 + (i & 7) * 16), 0x20);
            glVertex2f(x0, y0); glVertex2f(FPS_W - 8 + x0, y0);
            glVertex2f(FPS_W - 8 + x0, FPS_H - 8 + y0); glVertex2f(x0, FPS_H - 8 + y0);
        }
        glEnd();
        LARGE_INTEGER a, b;
        QueryPerformanceCounter(&a);
        if (!SwapBuffers(dc)) { printf("FAIL SwapBuffers\n"); return 1; }
        QueryPerformanceCounter(&b);
        swap += b.QuadPart - a.QuadPart;
        frames++;
    }
    glFinish();
    DWORD ms = GetTickCount() - t0;
    gpu_stats(s1);
    printf("fps-result %s|%lu|%lu|%llu|%llu|%llu|%llu|%llu\n", renderer ? renderer : "?", (unsigned long)frames,
           (unsigned long)ms, s1[ST_DIRECT] - s0[ST_DIRECT],
           (s1[ST_TO_HOST] - s0[ST_TO_HOST]) + (s1[ST_FROM_HOST] - s0[ST_FROM_HOST]) + (s1[ST_PAINTED] - s0[ST_PAINTED]) +
           (s1[ST_SCREEN] - s0[ST_SCREEN]), (unsigned long long)(swap * 1000000 / qf.QuadPart),
           s1[ST_DIRECT_US] - s0[ST_DIRECT_US], s1[ST_DIRECT_LOCK_US] - s0[ST_DIRECT_LOCK_US]);
    return 0;
}

typedef struct {
    double fps; unsigned long frames, direct; double kib;
    double swap_ms, ask_ms, lock_ms;      /* a frame: in SwapBuffers, asking the kernel, waiting there for the desktop */
    char renderer[256];
} FpsResult;

/* Run the child with GALLIUM_DRIVER=@driver (and NOVA_PRESENT=@present);
 * its frames per second (or -1), the renderer it reported, the frames
 * shown from the host and the KiB per frame through guest memory in @r */
static double fps_run(const char *driver, const char *present, int secs, FpsResult *r)
{
    memset(r, 0, sizeof(*r));
    r->fps = -1;
    char exe[MAX_PATH], cmd[2 * MAX_PATH], out[MAX_PATH];
    GetModuleFileNameA(NULL, exe, sizeof(exe));
    GetTempPathA(sizeof(out), out);
    lstrcatA(out, "gltest-fps.txt");
    DeleteFileA(out);
    snprintf(cmd, sizeof(cmd), "\"%s\" fpsrun %d \"%s\"", exe, secs, out);
    SetEnvironmentVariableA("GALLIUM_DRIVER", driver);
    SetEnvironmentVariableA("NOVA_PRESENT", present);
    STARTUPINFOA si = { sizeof(si) };
    PROCESS_INFORMATION pi;
    BOOL started = CreateProcessA(NULL, cmd, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi);
    SetEnvironmentVariableA("GALLIUM_DRIVER", NULL);
    SetEnvironmentVariableA("NOVA_PRESENT", NULL);
    if (!started) return -1;
    WaitForSingleObject(pi.hProcess, (DWORD)(secs + 600) * 1000);
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
    char line[512] = "";
    FILE *f = fopen(out, "r");
    if (!f) return -1;
    if (!fgets(line, sizeof(line), f)) line[0] = 0;
    fclose(f);
    char *bar = strchr(line, '|');
    if (strncmp(line, "fps-result ", 11) || !bar) return -1;
    *bar = 0;
    snprintf(r->renderer, sizeof(r->renderer), "%s", line + 11);
    unsigned long ms = 0;
    unsigned long long direct = 0, bytes = 0, swap = 0, ask = 0, lock = 0;
    if (sscanf(bar + 1, "%lu|%lu|%llu|%llu|%llu|%llu|%llu", &r->frames, &ms, &direct, &bytes, &swap, &ask, &lock) != 7 ||
        !ms || !r->frames)
        return -1;
    r->direct = (unsigned long)direct;
    r->kib = bytes / 1024.0 / r->frames;
    r->swap_ms = swap / 1000.0 / r->frames;
    r->ask_ms = ask / 1000.0 / r->frames;
    r->lock_ms = lock / 1000.0 / r->frames;
    return r->fps = r->frames * 1000.0 / ms;
}

static void fps_print(const char *name, const FpsResult *r)
{
    printf("%-10s %.2f frames/s, %lu of %lu frames shown from the host, %.0f KiB a frame through guest memory  (%s)\n",
           name, r->fps, r->direct, r->frames, r->kib, r->renderer);
    printf("%-10s a frame: %.1f ms in SwapBuffers, %.1f ms of it asking the kernel to show it, %.1f ms of that waiting "
           "for the desktop\n", "", r->swap_ms, r->ask_ms, r->lock_ms);
}

/* Add run @b to @a (both ran, or @a is marked failed) */
static void fps_add(FpsResult *a, const FpsResult *b)
{
    if (a->fps <= 0 || b->fps <= 0) { a->fps = -1; return; }
    double ms = a->frames * 1000.0 / a->fps + b->frames * 1000.0 / b->fps;
    unsigned long n = a->frames + b->frames;
    a->kib = (a->kib * a->frames + b->kib * b->frames) / n;
    a->swap_ms = (a->swap_ms * a->frames + b->swap_ms * b->frames) / n;
    a->ask_ms = (a->ask_ms * a->frames + b->ask_ms * b->frames) / n;
    a->lock_ms = (a->lock_ms * a->frames + b->lock_ms * b->frames) / n;
    a->frames += b->frames;
    a->direct += b->direct;
    a->fps = a->frames * 1000.0 / ms;
}

/* The KiB a frame through guest memory that direct presentation itself costs:
 * the frames the host did not show (@v.frames - @v.direct) went through the
 * GDI like @g's and cost what they cost, however many a slow runner made
 * (the "frames from the host" check already limits those); what is left is
 * the desktop's own traffic and the scene's commands, which must stay far
 * below a frame through the GDI */
static double direct_kib(const FpsResult *v, const FpsResult *g)
{
    double gdi = v->frames > v->direct ? (double)(v->frames - v->direct) / v->frames : 0;
    double kib = v->kib - gdi * g->kib;
    return kib > 0 ? kib : 0;
}

/* Under KVM the guest's own copies cost little and the host's drawing is
 * nearly the whole frame, so both ways take about as long there (within
 * a few per cent, either way from run to run); direct presentation must
 * still move far fewer bytes, and the times are printed */
static int under_kvm(void)
{
    unsigned a, b, c, d;
    __cpuid(0x40000000, a, b, c, d);
    return b == 0x4b4d564b && c == 0x564b4d56 && d == 0x4d;            /* "KVMKVMKVM" */
}

static int fps_test(int secs)
{
    FpsResult v, g, l, v2, g2;
    /* virgl both ways in turn, half the time each turn (direct, GDI, GDI,
     * direct): the host draws most of each frame, and whatever else it does
     * meanwhile weighs on both ways alike */
    int half = secs > 1 ? secs / 2 : 1;
    printf("fps: virgl for %d s, through the GDI for %d s, twice\n", 2 * half, 2 * half);
    fps_run("virgl", NULL, half, &v);
    fps_run("virgl", "gdi", half, &g);
    fps_run("virgl", "gdi", half, &g2);
    fps_run("virgl", NULL, half, &v2);
    printf("fps: turns: direct %.2f, GDI %.2f, GDI %.2f, direct %.2f frames/s\n", v.fps, g.fps, g2.fps, v2.fps);
    fps_add(&v, &v2);
    fps_add(&g, &g2);
    fps_print("virgl", &v);
    fps_print("virgl/GDI", &g);
    printf("fps: llvmpipe for %d s\n", secs);
    fps_run("llvmpipe", NULL, secs, &l);
    fps_print("llvmpipe", &l);
    check("virgl ran", v.fps > 0 && g.fps > 0);
    check("virgl is the renderer", strstr(v.renderer, "virgl") != NULL);
    check("llvmpipe ran", l.fps > 0);
    check("llvmpipe is the renderer", strstr(l.renderer, "llvmpipe") != NULL && !strstr(l.renderer, "virgl"));
    check("virgl beats llvmpipe", v.fps > 0 && l.fps > 0 && g.fps > l.fps);
    check("virgl shows its frames from the host", v.direct * 10 >= v.frames * 9);
    check("NOVA_PRESENT=gdi shows them through the GDI", g.fps > 0 && g.direct == 0);
    check("direct presentation moves fewer bytes", v.fps > 0 && g.fps > 0 && v.kib * 4 < g.kib);
    if (under_kvm())
        check("direct presentation moves far fewer bytes under KVM", v.fps > 0 && g.fps > 0 && direct_kib(&v, &g) * 8 < g.kib);
    else
        check("direct presentation is faster", v.fps > 0 && g.fps > 0 && v.fps > g.fps);
    if (v.fps > 0 && l.fps > 0) printf("virgl is %.1fx llvmpipe\n", v.fps / l.fps);
    if (v.fps > 0 && g.fps > 0)
        printf("direct presentation: %.1f ms a frame against %.1f ms, %.0f KiB against %.0f KiB through guest memory "
               "(%.0f KiB without the frames the host did not show)\n",
               1000.0 / v.fps, 1000.0 / g.fps, v.kib, g.kib, direct_kib(&v, &g));
    printf("gltest fps: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

/* ---- direct presentation ------------------------------------------------ */
#define PR_W 256
#define PR_H 192

static void quadrants(void)
{
    static const float c[4][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 1, 1, 1 } };   /* (top left first) */
    glEnable(GL_SCISSOR_TEST);
    for (int i = 0; i < 4; i++) {
        glScissor((i & 1) * PR_W / 2, (i & 2) ? 0 : PR_H / 2, PR_W / 2, PR_H / 2);
        glClearColor(c[i][0], c[i][1], c[i][2], 1);
        glClear(GL_COLOR_BUFFER_BIT);
    }
    glDisable(GL_SCISSOR_TEST);
}

/* Present for @ms; the frames, and the frames shown from the host */
static unsigned long present_for(HDC dc, DWORD ms, unsigned long *direct)
{
    unsigned long long a[ST_N], b[ST_N];
    unsigned long frames = 0;
    gpu_stats(a);
    for (DWORD t0 = GetTickCount(); GetTickCount() - t0 < ms; frames++) {
        pump();
        quadrants();
        SwapBuffers(dc);
    }
    glFinish();
    gpu_stats(b);
    *direct = (unsigned long)(b[ST_DIRECT] - a[ST_DIRECT]);
    return frames;
}

static int present_test(void)
{
    HDC dc;
    HGLRC rc;
    unsigned long long st[ST_N];
    HWND w = gl_window("Direct presentation", 100, 100, PR_W, PR_H, &dc, &rc);
    check("context", w != NULL);
    check("the kernel's counters", gpu_stats(st));
    if (!w) { printf("gltest present: %d passed, %d failed\n", pass, fail); return 1; }
    printf("GL_RENDERER %s\n", glGetString(GL_RENDERER));
    pointer_away();
    glViewport(0, 0, PR_W, PR_H);
    unsigned long d1, d2, d3, f;
    present_for(dc, 1000, &d1);                         /* (the window is shown and active) */
    POINT o = { 0, 0 };
    ClientToScreen(w, &o);
    printf("present: quadrants at %ld %ld %d %d\n", o.x, o.y, PR_W, PR_H);
    f = present_for(dc, 6000, &d1);
    printf("alone: %lu of %lu frames shown from the host\n", d1, f);
    check("frames shown from the host while the window is alone", f > 0 && d1 * 10 >= f * 9);

    HWND cover = CreateWindowA("gltest", "Cover", WS_OVERLAPPEDWINDOW | WS_VISIBLE, o.x + PR_W / 2, o.y + PR_H / 2,
                               200, 150, 0, 0, GetModuleHandleA(NULL), 0);
    present_for(dc, 1000, &d2);                         /* (the window is drawn) */
    f = present_for(dc, 3000, &d2);
    printf("covered: %lu of %lu frames shown from the host\n", d2, f);
    check("frames go through the GDI while a window covers it", cover && f > 0 && d2 == 0);

    DestroyWindow(cover);
    SetForegroundWindow(w);
    pointer_away();
    present_for(dc, 1000, &d3);
    f = present_for(dc, 3000, &d3);
    printf("uncovered: %lu of %lu frames shown from the host\n", d3, f);
    check("frames shown from the host again once it has gone", f > 0 && d3 * 10 >= f * 9);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    DestroyWindow(w);
    printf("gltest present: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

/* ---- direct presentation on the monitors -------------------------------- */
static BOOL CALLBACK monitor_n(HMONITOR m, HDC dc, LPRECT r, LPARAM l)
{
    HMONITOR *out = (HMONITOR *)l;
    if (!out[0]) out[0] = m;
    else if (!out[1]) out[1] = m;
    return TRUE;
}

/* Display @n's monitor (1 or 2): its rectangle, and its screen's pixels
 * per desktop pixel */
static int monitor_of(int n, RECT *r, int *scale)
{
    HMONITOR m[2] = { 0, 0 };
    EnumDisplayMonitors(NULL, NULL, monitor_n, (LPARAM)m);
    for (int i = 0; i < 2; i++) {
        MONITORINFOEXA mi;
        mi.cbSize = sizeof(mi);
        char dev[16];
        snprintf(dev, sizeof(dev), "\\\\.\\DISPLAY%d", n);
        if (!m[i] || !GetMonitorInfoA(m[i], (MONITORINFO *)&mi) || strcmp(mi.szDevice, dev)) continue;
        DEVMODEA dm;
        memset(&dm, 0, sizeof(dm));
        dm.dmSize = sizeof(dm);
        *r = mi.rcMonitor;
        *scale = EnumDisplaySettingsA(dev, ENUM_CURRENT_SETTINGS, &dm) && r->right > r->left
                     ? (int)dm.dmPelsWidth / (r->right - r->left) : 1;
        return 1;
    }
    return 0;
}

static int set_mode(const char *dev, DWORD w, DWORD h)
{
    DEVMODEA dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
    dm.dmPelsWidth = w;
    dm.dmPelsHeight = h;
    return ChangeDisplaySettingsExA(dev, &dm, NULL, 0, NULL) == DISP_CHANGE_SUCCESSFUL;
}

/* The window's client area 64 px into display @n, the quadrants presented
 * there (where the self-test looks: @label) and counted */
static void present_on(HWND w, HDC dc, int n, const char *label)
{
    RECT m;
    int scale = 1;
    char what[128];
    snprintf(what, sizeof(what), "%s: the monitor is there", label);
    check(what, monitor_of(n, &m, &scale));
    RECT r = { 0, 0, PR_W, PR_H };
    AdjustWindowRect(&r, WS_OVERLAPPEDWINDOW, FALSE);
    SetWindowPos(w, HWND_TOP, m.left + 64 + r.left, m.top + 64 + r.top, 0, 0, SWP_NOSIZE);
    SetForegroundWindow(w);
    pointer_away();
    unsigned long d, f;
    present_for(dc, 1000, &d);                          /* (the window is drawn there) */
    POINT o = { 0, 0 };
    ClientToScreen(w, &o);
    printf("present: %s quadrants at %ld %ld %d %d\n", label, (o.x - m.left) * scale, (o.y - m.top) * scale,
           PR_W * scale, PR_H * scale);
    f = present_for(dc, 6000, &d);
    printf("%s (scale %d): %lu of %lu frames shown from the host\n", label, scale, d, f);
    snprintf(what, sizeof(what), "frames shown from the host on %s", label);
    check(what, f > 0 && d * 10 >= f * 9);
}

static int monitors_test(void)
{
    HDC dc;
    HGLRC rc;
    RECT m2;
    int s2 = 1;
    HWND w = gl_window("Direct presentation", 100, 100, PR_W, PR_H, &dc, &rc);
    check("context", w != NULL);
    check("a second monitor", GetSystemMetrics(SM_CMONITORS) >= 2 && monitor_of(2, &m2, &s2));
    if (!w || fail) { printf("gltest monitors: %d passed, %d failed\n", pass, fail); return 1; }
    printf("GL_RENDERER %s\n", glGetString(GL_RENDERER));
    glViewport(0, 0, PR_W, PR_H);
    DEVMODEA was;
    memset(&was, 0, sizeof(was));
    was.dmSize = sizeof(was);
    EnumDisplaySettingsA("\\\\.\\DISPLAY2", ENUM_CURRENT_SETTINGS, &was);
    present_on(w, dc, 2, "display 2");
    int big = set_mode("\\\\.\\DISPLAY2", 2560, 1600);
    check("\\\\.\\DISPLAY2 in 2560x1600", big);
    if (big) {
        int s;
        RECT m;
        check("it is at scale 2", monitor_of(2, &m, &s) && s == 2);
        present_on(w, dc, 2, "display 2 at scale 2");
        present_on(w, dc, 1, "display 1 at scale 2");
        check("\\\\.\\DISPLAY2 back as it was", set_mode("\\\\.\\DISPLAY2", was.dmPelsWidth, was.dmPelsHeight));
    }
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    DestroyWindow(w);
    printf("gltest monitors: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

/* ---- the colour-depth test ---------------------------------------------- */
static const struct { int r, g, b, a; int must; } depths[] = {
    { 8, 8, 8, 0, 1 }, { 5, 6, 5, 0, 1 }, { 5, 5, 5, 1, 0 }, { 4, 4, 4, 4, 1 }, { 10, 10, 10, 2, 0 }};
static const unsigned char bars[4][3] = { { 255, 0, 0 }, { 0, 255, 0 }, { 0, 0, 255 }, { 0x88, 0x88, 0x88 } };

/* The window pixel format with exactly these bits, or 0 */
static int format_with(HDC dc, int r, int g, int b, int a)
{
    PIXELFORMATDESCRIPTOR p;
    int n = DescribePixelFormat(dc, 1, sizeof(p), &p);
    for (int i = 1; i <= n; i++) {
        if (!DescribePixelFormat(dc, i, sizeof(p), &p)) continue;
        DWORD need = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        if ((p.dwFlags & need) == need && p.iPixelType == PFD_TYPE_RGBA && p.cRedBits == r && p.cGreenBits == g &&
            p.cBlueBits == b && p.cAlphaBits == a)
            return i;
    }
    return 0;
}

static int colors_test(void)
{
    for (unsigned k = 0; k < sizeof(depths) / sizeof(depths[0]); k++) {
        char title[64], what[96];
        int R = depths[k].r, G = depths[k].g, B = depths[k].b, A = depths[k].a;
        snprintf(title, sizeof(title), "OpenGL %d:%d:%d:%d", R, G, B, A);
        RECT rr = { 0, 0, 256, 64 };
        AdjustWindowRect(&rr, WS_OVERLAPPEDWINDOW, FALSE);
        HWND w = CreateWindowA("gltest", title, WS_OVERLAPPEDWINDOW | WS_VISIBLE, 120, 120, rr.right - rr.left,
                               rr.bottom - rr.top, 0, 0, GetModuleHandleA(NULL), 0);
        HDC dc = GetDC(w);
        int fmt = format_with(dc, R, G, B, A);
        if (!fmt) {
            printf("%s: not offered\n", title);
            if (depths[k].must) check(title, 0);
            ReleaseDC(w, dc); DestroyWindow(w);
            continue;
        }
        PIXELFORMATDESCRIPTOR pfd;
        DescribePixelFormat(dc, fmt, sizeof(pfd), &pfd);
        HGLRC rc = NULL;
        int ok = SetPixelFormat(dc, fmt, &pfd) && (rc = wglCreateContext(dc)) && wglMakeCurrent(dc, rc);
        snprintf(what, sizeof(what), "%s context", title);
        check(what, ok);
        if (!ok) { ReleaseDC(w, dc); DestroyWindow(w); continue; }
        RECT cr; GetClientRect(w, &cr);
        glViewport(0, 0, cr.right, cr.bottom);
        glMatrixMode(GL_PROJECTION); glLoadIdentity(); glOrtho(0, 4, 0, 1, -1, 1);
        for (int f = 0; f < 3; f++) {                       /* a few frames: the window is surely shown */
            pump();
            glClearColor(0, 0, 0, 1);
            glClear(GL_COLOR_BUFFER_BIT);
            glBegin(GL_QUADS);
            for (int i = 0; i < 4; i++) {
                glColor3ub(bars[i][0], bars[i][1], bars[i][2]);
                glVertex2f((float)i, 0); glVertex2f(i + 1.0f, 0); glVertex2f(i + 1.0f, 1); glVertex2f((float)i, 1);
            }
            glEnd();
            SwapBuffers(dc);
            Sleep(100);
        }
        glFinish();
        int good = 1;
        printf("%s:", title);
        for (int i = 0; i < 4; i++) {
            COLORREF c = GetPixel(dc, cr.right * (2 * i + 1) / 8, cr.bottom / 2);
            unsigned char px[3] = { GetRValue(c), GetGValue(c), GetBValue(c) };
            printf(" %02x%02x%02x", px[0], px[1], px[2]);
            if (c == CLR_INVALID || abs(px[0] - bars[i][0]) > 20 || abs(px[1] - bars[i][1]) > 20 || abs(px[2] - bars[i][2]) > 20)
                good = 0;
        }
        printf("\n");
        snprintf(what, sizeof(what), "%s colours", title);
        check(what, good);
        wglMakeCurrent(NULL, NULL);
        wglDeleteContext(rc);
        ReleaseDC(w, dc);
        DestroyWindow(w);
    }
    printf("gltest colors: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    if (argc > 1 && (!strcmp(argv[1], "fpsrun") || !strcmp(argv[1], "fps") || !strcmp(argv[1], "colors") ||
                     !strcmp(argv[1], "present") || !strcmp(argv[1], "monitors"))) {
        WNDCLASSA wc = { CS_OWNDC, proc, 0, 0, GetModuleHandleA(NULL), 0, LoadCursor(NULL, IDC_ARROW), 0, 0, "gltest" };
        RegisterClassA(&wc);
        if (!strcmp(argv[1], "colors")) {
            char drv[32];
            if (argc > 2 && (!GetEnvironmentVariableA("GALLIUM_DRIVER", drv, sizeof(drv)) || strcmp(drv, argv[2]))) {
                char exe[MAX_PATH], cmd[2 * MAX_PATH];  /* opengl32 picks its driver when it loads */
                GetModuleFileNameA(NULL, exe, sizeof(exe));
                snprintf(cmd, sizeof(cmd), "\"%s\" colors %s", exe, argv[2]);
                SetEnvironmentVariableA("GALLIUM_DRIVER", argv[2]);
                STARTUPINFOA si = { sizeof(si) };
                PROCESS_INFORMATION pi;
                DWORD code = 1;
                if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) { printf("FAIL CreateProcess\n"); return 1; }
                WaitForSingleObject(pi.hProcess, INFINITE);
                GetExitCodeProcess(pi.hProcess, &code);
                CloseHandle(pi.hThread);
                CloseHandle(pi.hProcess);
                return (int)code;
            }
            return colors_test();
        }
        if (!strcmp(argv[1], "fps")) return fps_test(argc > 2 ? atoi(argv[2]) : 10);
        if (!strcmp(argv[1], "present")) return present_test();
        if (!strcmp(argv[1], "monitors")) return monitors_test();
        if (argc > 3) freopen(argv[3], "w", stdout);   /* the child: fpsrun SECS FILE */
        return fps_child(argc > 2 ? atoi(argv[2]) : 5);
    }
    int seconds = argc > 1 ? atoi(argv[1]) : 3;
    const char *want = argc > 2 ? argv[2] : NULL;
    char drv[32];
    if (want && (!GetEnvironmentVariableA("GALLIUM_DRIVER", drv, sizeof(drv)) || strcmp(drv, want))) {
        /* opengl32 picks its driver when it loads: run again with GALLIUM_DRIVER=RENDERER */
        char exe[MAX_PATH], cmd[2 * MAX_PATH];
        GetModuleFileNameA(NULL, exe, sizeof(exe));
        snprintf(cmd, sizeof(cmd), "\"%s\" %d %s", exe, seconds, want);
        SetEnvironmentVariableA("GALLIUM_DRIVER", want);
        STARTUPINFOA si = { sizeof(si) };
        PROCESS_INFORMATION pi;
        DWORD code = 1;
        if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi)) { printf("FAIL CreateProcess\n"); return 1; }
        WaitForSingleObject(pi.hProcess, INFINITE);
        GetExitCodeProcess(pi.hProcess, &code);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        return (int)code;
    }
    WNDCLASSA wc = { CS_OWNDC, proc, 0, 0, GetModuleHandleA(NULL), 0, LoadCursor(NULL, IDC_ARROW), 0, 0, "gltest" };
    RegisterClassA(&wc);
    HWND w = CreateWindowA("gltest", "OpenGL test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 336, 279, 0, 0, wc.hInstance, 0);
    HDC dc = GetDC(w);
    RECT cr; GetClientRect(w, &cr);
    int W = cr.right, H = cr.bottom;
    PIXELFORMATDESCRIPTOR pfd = { sizeof(pfd), 1, PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
                                  PFD_TYPE_RGBA, 32, 0, 0, 0, 0, 0, 0, 8, 0, 0, 0, 0, 0, 0, 24, 8, 0, PFD_MAIN_PLANE, 0, 0, 0, 0 };
    int fmt = ChoosePixelFormat(dc, &pfd);
    check("ChoosePixelFormat", fmt > 0);
    PIXELFORMATDESCRIPTOR got;
    check("DescribePixelFormat", DescribePixelFormat(dc, fmt, sizeof(got), &got) > 0 && got.cColorBits >= 24);
    check("SetPixelFormat", SetPixelFormat(dc, fmt, &pfd));
    check("GetPixelFormat", GetPixelFormat(dc) == fmt);
    HGLRC rc = wglCreateContext(dc);
    check("wglCreateContext", rc != NULL);
    check("wglMakeCurrent", wglMakeCurrent(dc, rc));
    if (!rc) { printf("gltest: %d passed, %d failed\n", pass, fail); return 1; }
    printf("GL_VENDOR   %s\nGL_RENDERER %s\nGL_VERSION  %s\nGLSL        %s\n", glGetString(GL_VENDOR),
           glGetString(GL_RENDERER), glGetString(GL_VERSION), glGetString(GL_SHADING_LANGUAGE_VERSION));
    check("version", glGetString(GL_VERSION) && atof((const char *)glGetString(GL_VERSION)) >= 3.0);
    if (want) check("renderer", glGetString(GL_RENDERER) && strstr((const char *)glGetString(GL_RENDERER), want));

    unsigned char px[4];
    glViewport(0, 0, W, H);
    glClearColor(0.2f, 0.4f, 0.6f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check("clear", px_near(px, 51, 102, 153));

    glBegin(GL_TRIANGLES);                                 /* fixed function */
    glColor3f(1, 0, 0); glVertex2f(-1, -1); glVertex2f(3, -1); glVertex2f(-1, 3);
    glEnd();
    glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
    check("triangle", px_near(px, 255, 0, 0));

#define LOAD(n, s) (n = (void *)wglGetProcAddress(s))
    LOAD(pCreateShader, "glCreateShader"); LOAD(pShaderSource, "glShaderSource"); LOAD(pCompileShader, "glCompileShader");
    LOAD(pGetShaderiv, "glGetShaderiv"); LOAD(pCreateProgram, "glCreateProgram"); LOAD(pAttachShader, "glAttachShader");
    LOAD(pLinkProgram, "glLinkProgram"); LOAD(pGetProgramiv, "glGetProgramiv"); LOAD(pUseProgram, "glUseProgram");
    LOAD(pGetUniformLocation, "glGetUniformLocation"); LOAD(pUniform1f, "glUniform1f"); LOAD(pGetShaderInfoLog, "glGetShaderInfoLog");
    check("wglGetProcAddress", pCreateShader && pUseProgram && pUniform1f);
    GLuint prog = 0;
    GLint loc = -1;
    if (pCreateShader) {
        GLuint vs = shader(GL_VERTEX_SHADER,
            "#version 120\nuniform float t; varying vec3 c;\n"
            "void main(){ float a=t; mat2 r=mat2(cos(a),sin(a),-sin(a),cos(a));\n"
            " gl_Position=vec4(r*gl_Vertex.xy,0.0,1.0); c=gl_Color.rgb; }\n");
        GLuint fs = shader(GL_FRAGMENT_SHADER,
            "#version 120\nvarying vec3 c; void main(){ gl_FragColor=vec4(c,1.0); }\n");
        prog = pCreateProgram();
        pAttachShader(prog, vs); pAttachShader(prog, fs); pLinkProgram(prog);
        GLint ok = 0; pGetProgramiv(prog, GL_LINK_STATUS, &ok);
        check("link", ok);
        pUseProgram(prog);
        loc = pGetUniformLocation(prog, "t");
        pUniform1f(loc, 0.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        glBegin(GL_TRIANGLES);
        glColor3f(0, 1, 0); glVertex2f(-1, -1); glVertex2f(3, -1); glVertex2f(-1, 3);
        glEnd();
        glReadPixels(W / 2, H / 2, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, px);
        check("shader", px_near(px, 0, 255, 0));
    }
    check("SwapBuffers", SwapBuffers(dc));

    /* Animated frames */
    DWORD start = GetTickCount(), frames = 0;
    MSG m;
    int quit = 0;
    while (!quit && GetTickCount() - start < (DWORD)seconds * 1000) {
        while (PeekMessageA(&m, 0, 0, 0, PM_REMOVE)) { if (m.message == WM_QUIT) quit = 1; DispatchMessageA(&m); }
        float t = (GetTickCount() - start) / 1000.0f;
        glClearColor(0.1f, 0.1f, 0.15f, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        if (prog) pUniform1f(loc, t);
        glBegin(GL_TRIANGLES);
        glColor3f(1, 0.3f, 0.2f); glVertex2f(0, 0.8f);
        glColor3f(0.2f, 1, 0.3f); glVertex2f(-0.7f, -0.5f);
        glColor3f(0.2f, 0.4f, 1); glVertex2f(0.7f, -0.5f);
        glEnd();
        SwapBuffers(dc);
        frames++;
    }
    DWORD ms = GetTickCount() - start;
    check("frames", frames > 0);
    printf("%lu frames in %lu ms (%.1f fps)\n", frames, ms, ms ? frames * 1000.0 / ms : 0.0);
    wglMakeCurrent(NULL, NULL);
    wglDeleteContext(rc);
    ReleaseDC(w, dc);
    DestroyWindow(w);
    printf("gltest: %d passed, %d failed\n", pass, fail);
    return fail != 0;
}

