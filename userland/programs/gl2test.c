/* gl2test.exe — OpenGL 2 and later with nothing installed: drive C: carries
 * Mesa's llvmpipe from the start (tools/bundle_mesa.py) and NovaOS's
 * opengl32.dll hands programs to it, so a program that needs shaders
 * (Krita's canvas, Qt Quick, SDL games) gets them as on a Windows machine
 * with a display driver's OpenGL.  It checks the version and renderer,
 * compiles and links a GLSL program, draws with it and reads the pixels
 * back, swaps them to the window, and makes an OpenGL 3.3 core-profile
 * context with wglCreateContextAttribsARB. */
#include <windows.h>
#include <stdio.h>
#include <string.h>

/* wingdi.h's pixel formats */
typedef struct tagPIXELFORMATDESCRIPTOR {
    WORD nSize, nVersion;
    DWORD dwFlags;
    BYTE iPixelType, cColorBits, cRedBits, cRedShift, cGreenBits, cGreenShift, cBlueBits, cBlueShift,
         cAlphaBits, cAlphaShift, cAccumBits, cAccumRedBits, cAccumGreenBits, cAccumBlueBits, cAccumAlphaBits,
         cDepthBits, cStencilBits, cAuxBuffers, iLayerType, bReserved;
    DWORD dwLayerMask, dwVisibleMask, dwDamageMask;
} PIXELFORMATDESCRIPTOR;
#define PFD_DOUBLEBUFFER 0x1
#define PFD_DRAW_TO_WINDOW 0x4
#define PFD_SUPPORT_OPENGL 0x20
#define PFD_TYPE_RGBA 0
__declspec(dllimport) int WINAPI ChoosePixelFormat(HDC, const PIXELFORMATDESCRIPTOR *);
__declspec(dllimport) BOOL WINAPI SetPixelFormat(HDC, int, const PIXELFORMATDESCRIPTOR *);
__declspec(dllimport) BOOL WINAPI SwapBuffers(HDC);
typedef HANDLE HGLRC;

#define GL_RENDERER 0x1F01
#define GL_VERSION 0x1F02
#define GL_SHADING_LANGUAGE_VERSION 0x8B8C
#define GL_COLOR_BUFFER_BIT 0x4000
#define GL_RGBA 0x1908
#define GL_UNSIGNED_BYTE 0x1401
#define GL_FLOAT 0x1406
#define GL_TRIANGLES 4
#define GL_FRAGMENT_SHADER 0x8B30
#define GL_VERTEX_SHADER 0x8B31
#define GL_COMPILE_STATUS 0x8B81
#define GL_LINK_STATUS 0x8B82
#define WGL_CONTEXT_MAJOR_VERSION_ARB 0x2091
#define WGL_CONTEXT_MINOR_VERSION_ARB 0x2092
#define WGL_CONTEXT_PROFILE_MASK_ARB 0x9126
#define WGL_CONTEXT_CORE_PROFILE_BIT_ARB 0x1

/* from opengl32.dll by name, and the rest from wglGetProcAddress */
static HGLRC (WINAPI *wglCreateContext)(HDC);
static BOOL (WINAPI *wglMakeCurrent)(HDC, HGLRC);
static BOOL (WINAPI *wglDeleteContext)(HGLRC);
static FARPROC (WINAPI *wglGetProcAddress)(LPCSTR);
static const unsigned char *(WINAPI *glGetString)(unsigned);
static void (WINAPI *glClearColor)(float, float, float, float);
static void (WINAPI *glClear)(unsigned);
static void (WINAPI *glReadPixels)(int, int, int, int, unsigned, unsigned, void *);
static unsigned (WINAPI *glGetError)(void);
static void (WINAPI *glDrawArrays)(unsigned, int, int);
static HGLRC (WINAPI *wglCreateContextAttribsARB)(HDC, HGLRC, const int *);
static unsigned (WINAPI *glCreateShader)(unsigned);
static void (WINAPI *glShaderSource)(unsigned, int, const char *const *, const int *);
static void (WINAPI *glCompileShader)(unsigned);
static void (WINAPI *glGetShaderiv)(unsigned, unsigned, int *);
static unsigned (WINAPI *glCreateProgram)(void);
static void (WINAPI *glAttachShader)(unsigned, unsigned);
static void (WINAPI *glBindAttribLocation)(unsigned, unsigned, const char *);
static void (WINAPI *glLinkProgram)(unsigned);
static void (WINAPI *glGetProgramiv)(unsigned, unsigned, int *);
static void (WINAPI *glUseProgram)(unsigned);
static int (WINAPI *glGetUniformLocation)(unsigned, const char *);
static void (WINAPI *glUniform4f)(int, float, float, float, float);
static void (WINAPI *glVertexAttribPointer)(unsigned, int, unsigned, unsigned char, int, const void *);
static void (WINAPI *glEnableVertexAttribArray)(unsigned);

static int passed, failed;
static void check(const char *what, int ok)
{
    printf("%s: %s\n", ok ? "PASS" : "FAIL", what);
    if (ok) passed++; else failed++;
}

static unsigned shader(unsigned kind, const char *src)
{
    unsigned s = glCreateShader(kind);
    int ok = 0;
    glShaderSource(s, 1, &src, 0);
    glCompileShader(s);
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    return ok ? s : 0;
}

int main(void)
{
    HMODULE gl = LoadLibraryA("opengl32.dll");
    check("opengl32.dll loads", gl != 0);
    if (!gl) { printf("gl2test: %d passed, %d failed\n", passed, failed); return 1; }
#define LOAD(n) *(FARPROC *)&n = GetProcAddress(gl, #n)
    LOAD(wglCreateContext); LOAD(wglMakeCurrent); LOAD(wglDeleteContext); LOAD(wglGetProcAddress);
    LOAD(glGetString); LOAD(glClearColor); LOAD(glClear); LOAD(glReadPixels); LOAD(glGetError); LOAD(glDrawArrays);

    WNDCLASSA wc;
    memset(&wc, 0, sizeof(wc));
    wc.lpfnWndProc = DefWindowProcA;
    wc.hInstance = GetModuleHandleA(0);
    wc.lpszClassName = "gl2test";
    wc.style = CS_OWNDC;
    RegisterClassA(&wc);
    HWND w = CreateWindowA("gl2test", "gl2test", WS_OVERLAPPEDWINDOW | WS_VISIBLE, 100, 100, 320, 240, 0, 0, wc.hInstance, 0);
    HDC dc = GetDC(w);
    PIXELFORMATDESCRIPTOR pfd;
    memset(&pfd, 0, sizeof(pfd));
    pfd.nSize = sizeof(pfd);
    pfd.nVersion = 1;
    pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
    pfd.iPixelType = PFD_TYPE_RGBA;
    pfd.cColorBits = 32;
    int fmt = ChoosePixelFormat(dc, &pfd);
    check("ChoosePixelFormat, SetPixelFormat", fmt > 0 && SetPixelFormat(dc, fmt, &pfd));
    HGLRC rc = wglCreateContext(dc);
    check("wglCreateContext, wglMakeCurrent", rc && wglMakeCurrent(dc, rc));
    const char *ver = (const char *)glGetString(GL_VERSION), *ren = (const char *)glGetString(GL_RENDERER);
    const char *sl = (const char *)glGetString(GL_SHADING_LANGUAGE_VERSION);
    printf("GL_VERSION %s, GL_RENDERER %s, GLSL %s\n", ver ? ver : "(none)", ren ? ren : "(none)", sl ? sl : "(none)");
    check("OpenGL 3.0 or later", ver && ver[0] >= '3' && ver[0] <= '9' && ver[1] == '.');
    check("Mesa's llvmpipe with no driver installed", ren && strstr(ren, "llvmpipe"));

#define PROC(n) *(FARPROC *)&n = wglGetProcAddress(#n)
    PROC(glCreateShader); PROC(glShaderSource); PROC(glCompileShader); PROC(glGetShaderiv);
    PROC(glCreateProgram); PROC(glAttachShader); PROC(glBindAttribLocation); PROC(glLinkProgram);
    PROC(glGetProgramiv); PROC(glUseProgram); PROC(glGetUniformLocation); PROC(glUniform4f);
    PROC(glVertexAttribPointer); PROC(glEnableVertexAttribArray); PROC(wglCreateContextAttribsARB);
    int procs = glCreateShader && glShaderSource && glCompileShader && glGetShaderiv && glCreateProgram &&
                glAttachShader && glBindAttribLocation && glLinkProgram && glGetProgramiv && glUseProgram &&
                glGetUniformLocation && glUniform4f && glVertexAttribPointer && glEnableVertexAttribArray;
    check("wglGetProcAddress: the OpenGL 2 shader functions", procs);
    if (procs) {
        unsigned vs = shader(GL_VERTEX_SHADER, "#version 110\nattribute vec2 pos;\n"
                                               "void main() { gl_Position = vec4(pos, 0.0, 1.0); }\n");
        unsigned fs = shader(GL_FRAGMENT_SHADER, "#version 110\nuniform vec4 colour;\n"
                                                 "void main() { gl_FragColor = colour; }\n");
        check("GLSL: vertex and fragment shaders compile", vs && fs);
        unsigned prog = glCreateProgram();
        int linked = 0;
        glAttachShader(prog, vs);
        glAttachShader(prog, fs);
        glBindAttribLocation(prog, 0, "pos");
        glLinkProgram(prog);
        glGetProgramiv(prog, GL_LINK_STATUS, &linked);
        check("GLSL: the program links", linked);

        /* black, then a green triangle over the window's left half from the shader */
        glClearColor(0, 0, 0, 1);
        glClear(GL_COLOR_BUFFER_BIT);
        static const float tri[6] = { -1, -1, 0, -1, -1, 1 };
        glUseProgram(prog);
        glUniform4f(glGetUniformLocation(prog, "colour"), 0, 1, 0, 1);
        glVertexAttribPointer(0, 2, GL_FLOAT, 0, 0, tri);
        glEnableVertexAttribArray(0);
        glDrawArrays(GL_TRIANGLES, 0, 3);
        unsigned char in[4] = { 0 }, out[4] = { 0 };
        glReadPixels(10, 10, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, in);           /* bottom left: inside */
        glReadPixels(300, 200, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, out);        /* top right: outside */
        check("a shader-drawn triangle: green inside, black outside",
              in[0] == 0 && in[1] == 255 && in[2] == 0 && out[0] == 0 && out[1] == 0 && out[2] == 0);
        check("no errors", glGetError() == 0);
        check("SwapBuffers", SwapBuffers(dc));
        RECT r;
        GetClientRect(w, &r);
        COLORREF c = GetPixel(dc, 10, r.bottom - 11);
        check("the window shows the triangle", c == RGB(0, 255, 0));
    }

    /* an OpenGL 3.3 core-profile context, as Qt and SDL ask for one */
    static const int attribs[] = { WGL_CONTEXT_MAJOR_VERSION_ARB, 3, WGL_CONTEXT_MINOR_VERSION_ARB, 3,
                                   WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB, 0 };
    HGLRC core = wglCreateContextAttribsARB ? wglCreateContextAttribsARB(dc, 0, attribs) : 0;
    const char *cver = core && wglMakeCurrent(dc, core) ? (const char *)glGetString(GL_VERSION) : 0;
    printf("core profile GL_VERSION %s\n", cver ? cver : "(none)");
    check("wglCreateContextAttribsARB: an OpenGL 3.3 core-profile context",
          cver && (cver[0] > '3' || (cver[0] == '3' && cver[2] >= '3')) && cver[0] <= '9');
    wglMakeCurrent(0, 0);
    if (core) wglDeleteContext(core);
    check("wglDeleteContext", rc && wglDeleteContext(rc));
    ReleaseDC(w, dc);
    DestroyWindow(w);
    printf("gl2test: %d passed, %d failed\n", passed, failed);
    return failed ? 1 : 0;
}
