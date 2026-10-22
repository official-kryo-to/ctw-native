// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 Kryo.to
// See LICENSE in the repository root.
#include "os.h"
#include <SDL.h>
#include <glad/gl.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;

static SDL_Window* g_win;
static SDL_GLContext g_gl;
static bool g_relativeRequested = false, g_inputFocused = false;
static int g_w = 1280, g_h = 720;
static std::string g_resRoot = "data", g_docRoot = "saves";
static auto g_t0 = std::chrono::steady_clock::now();

// ---------------------------------------------------------------- files
// The APK stores asset names in mixed case and the game is inconsistent about it,
// so resolve paths case-insensitively.
static std::string resolve(const std::string& root, const char* rel) {
    fs::path p = root;
    std::string r = rel;
    for (auto& c : r) if (c == '\\') c = '/';
    size_t start = 0;
    while (start < r.size()) {
        size_t end = r.find('/', start);
        std::string part = r.substr(start, end == std::string::npos ? end : end - start);
        start = end == std::string::npos ? r.size() : end + 1;
        if (part.empty() || part == ".") continue;
        fs::path direct = p / part;
        std::error_code ec;
        if (fs::exists(direct, ec)) { p = direct; continue; }
        bool found = false;
        if (fs::is_directory(p, ec)) {
            for (auto& e : fs::directory_iterator(p, ec)) {
                std::string n = e.path().filename().string();
                if (SDL_strcasecmp(n.c_str(), part.c_str()) == 0) { p = e.path(); found = true; break; }
            }
        }
        if (!found) p = direct;
    }
    return p.string();
}

struct OSFile { FILE* fp = nullptr; };

void OS_SetResourceRoot(const char* d) { g_resRoot = d; }
void OS_SetDocumentsRoot(const char* d) { g_docRoot = d; }   // created on the first write

int OS_FileOpen(OSFileDataArea area, void** out, const char* path, OSFileAccessType acc) {
    bool docs = area == OS_AREA_DOCUMENTS || acc == OS_FILE_WRITE || acc == OS_FILE_READWRITE;
    std::string full = resolve(docs ? g_docRoot : g_resRoot, path);
    if (acc == OS_FILE_WRITE || acc == OS_FILE_READWRITE) {
        std::error_code ec;
        fs::create_directories(fs::path(full).parent_path(), ec);
    }
    FILE* fp = nullptr;
    switch (acc) {
        case OS_FILE_WRITE: fp = fopen(full.c_str(), "wb"); break;
        case OS_FILE_READWRITE:
            fp = fopen(full.c_str(), "rb+");
            if (!fp) fp = fopen(full.c_str(), "wb+");
            break;
        default: fp = fopen(full.c_str(), "rb"); break;
    }
    if (!fp) { *out = nullptr; return OS_FAIL; }
    *out = new OSFile{fp};
    return OS_OK;
}
int OS_FileRead(void* f, void* dst, int n) {
    if (n == 0) return OS_OK;
    size_t got = fread(dst, 1, (size_t)n, ((OSFile*)f)->fp);
    return got == (size_t)n ? OS_OK : OS_EOF;
}
int OS_FileWrite(void* f, void* src, int n) {
    return fwrite(src, 1, (size_t)n, ((OSFile*)f)->fp) == (size_t)n ? OS_OK : OS_FAIL;
}
int OS_FileSetPosition(void* f, int pos) { return fseek(((OSFile*)f)->fp, pos, SEEK_SET) == 0 ? OS_OK : OS_SEEK_ERROR; }
int OS_FileGetPosition(void* f) { return (int)ftell(((OSFile*)f)->fp); }
int OS_FileSize(void* f) {
    FILE* fp = ((OSFile*)f)->fp;
    long cur = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long sz = ftell(fp);
    fseek(fp, cur, SEEK_SET);
    return (int)sz;
}
int OS_FileClose(void* f) { if (f) { fclose(((OSFile*)f)->fp); delete (OSFile*)f; } return OS_OK; }
int OS_FileFlush(void* f) { return fflush(((OSFile*)f)->fp) == 0 ? OS_OK : OS_FAIL; }
int OS_FileDelete(OSFileDataArea, const char* path) { return remove(resolve(g_docRoot, path).c_str()) == 0 ? OS_OK : OS_FAIL; }

// ---------------------------------------------------------------- time
double OS_TimeAccurate() {
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - g_t0).count();
}
int OS_TimeMS() { return (int)(OS_TimeAccurate() * 1000.0); }
void OS_ThreadSleep(int us) { if (us > 0) SDL_Delay((Uint32)((us + 999) / 1000)); }   // round up: never a 0 ms busy loop

// ---------------------------------------------------------------- threads
struct OSThread { SDL_Thread* t; OSThreadFunc fn; void* arg; };
static int thunk(void* p) { auto* t = (OSThread*)p; return (int)t->fn(t->arg); }

void* OS_ThreadLaunch(OSThreadFunc fn, void* arg, unsigned, const char* name, void*, int) {
    auto* t = new OSThread{nullptr, fn, arg};
    t->t = SDL_CreateThread(thunk, name ? name : "OSThread", t);
    return t;
}
void OS_ThreadWait(void* t) {
    auto* th = (OSThread*)t;
    if (!th || !th->t) return;
    int r;
    SDL_WaitThread(th->t, &r);
    th->t = nullptr;
}
void OS_ThreadClose(void* t) { delete (OSThread*)t; }
void* OS_MutexCreate(const char*) { return SDL_CreateMutex(); }
void OS_MutexDelete(void* m) { SDL_DestroyMutex((SDL_mutex*)m); }
void OS_MutexObtain(void* m) { SDL_LockMutex((SDL_mutex*)m); }
void OS_MutexRelease(void* m) { SDL_UnlockMutex((SDL_mutex*)m); }
void* OS_SemaphoreCreate() { return SDL_CreateSemaphore(0); }
void OS_SemaphoreDelete(void* s) { SDL_DestroySemaphore((SDL_sem*)s); }
void OS_SemaphorePost(void* s) { SDL_SemPost((SDL_sem*)s); }
void OS_SemaphoreWait(void* s) { SDL_SemWait((SDL_sem*)s); }
bool OS_SemaphoreTryWait(void* s) { return SDL_SemTryWait((SDL_sem*)s) == 0; }

// ---------------------------------------------------------------- screen
unsigned OS_ScreenGetWidth() { return (unsigned)g_w; }
unsigned OS_ScreenGetHeight() { return (unsigned)g_h; }
void OS_ScreenSwapBuffers() { SDL_GL_SwapWindow(g_win); }
bool OS_ShowingSplashScreen() { return false; }

// ---------------------------------------------------------------- host
bool Host_Init(const char* title, int w, int h) {
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return false;
    }
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_COMPATIBILITY);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);
    SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 8);
    g_win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                             SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!g_win) { fprintf(stderr, "window: %s\n", SDL_GetError()); return false; }
    g_inputFocused = (SDL_GetWindowFlags(g_win) & SDL_WINDOW_INPUT_FOCUS) != 0;
    g_gl = SDL_GL_CreateContext(g_win);
    if (!g_gl || !gladLoadGL((GLADloadfunc)SDL_GL_GetProcAddress)) { fprintf(stderr, "GL init failed\n"); return false; }
    // SDL can return a legacy context when Windows has no modern OpenGL driver.
    if (!GLAD_GL_VERSION_3_3) {
        fprintf(stderr, "OpenGL 3.3 compatibility is required; got %s | %s\n",
                glGetString(GL_VERSION), glGetString(GL_RENDERER));
        return false;
    }
    SDL_GL_SetSwapInterval(1);
    SDL_GL_GetDrawableSize(g_win, &g_w, &g_h);
    printf("GL: %s | %s\n", glGetString(GL_VERSION), glGetString(GL_RENDERER));
    return true;
}
#include <deque>
static std::deque<int> g_keys;
static SDL_GameController* g_pad = nullptr;
static HostPad g_padState, g_padTest;
static bool g_padTesting = false;
const HostPad& Host_Pad() { return g_padTesting ? g_padTest : g_padState; }
void Host_TestPad(const HostPad* pad) { g_padTesting = pad != nullptr; if (pad) g_padTest = *pad; }
static void padOpen() {
    if (g_pad) return;
    for (int i = 0; i < SDL_NumJoysticks(); ++i)
        if (SDL_IsGameController(i) && (g_pad = SDL_GameControllerOpen(i))) return;
}
static float padStick(Sint16 v) { return v < 0 ? v / 32768.f : v / 32767.f; }
static void padRead() {
    HostPad p;
    if (g_pad && SDL_GameControllerGetAttached(g_pad)) {
        p.connected = true;
        auto stick = [](float& x, float& y, SDL_GameControllerAxis ax, SDL_GameControllerAxis ay) {
            x = padStick(SDL_GameControllerGetAxis(g_pad, ax));
            y = -padStick(SDL_GameControllerGetAxis(g_pad, ay));
            const float m = std::sqrt(x * x + y * y), dead = 0.24f;
            if (m <= dead) { x = y = 0; return; }
            const float k = std::min(1.f, (m - dead) / (1.f - dead)) / m;
            x *= k; y *= k;
        };
        stick(p.lx, p.ly, SDL_CONTROLLER_AXIS_LEFTX, SDL_CONTROLLER_AXIS_LEFTY);
        stick(p.rx, p.ry, SDL_CONTROLLER_AXIS_RIGHTX, SDL_CONTROLLER_AXIS_RIGHTY);
        auto trigger = [](SDL_GameControllerAxis a) {
            const float t = SDL_GameControllerGetAxis(g_pad, a) / 32767.f;
            return t < 0.1f ? 0.f : std::min(1.f, (t - 0.1f) / 0.9f);
        };
        p.lt = trigger(SDL_CONTROLLER_AXIS_TRIGGERLEFT);
        p.rt = trigger(SDL_CONTROLLER_AXIS_TRIGGERRIGHT);
        auto held = [](SDL_GameControllerButton b) { return SDL_GameControllerGetButton(g_pad, b) != 0; };
        p.a = held(SDL_CONTROLLER_BUTTON_A); p.b = held(SDL_CONTROLLER_BUTTON_B);
        p.x = held(SDL_CONTROLLER_BUTTON_X); p.y = held(SDL_CONTROLLER_BUTTON_Y);
        p.lb = held(SDL_CONTROLLER_BUTTON_LEFTSHOULDER); p.rb = held(SDL_CONTROLLER_BUTTON_RIGHTSHOULDER);
    }
    g_padState = p;
}
static int padKey(int button) {   // the keyboard key a controller button stands for
    switch (button) {
    case SDL_CONTROLLER_BUTTON_Y: return SDL_SCANCODE_F;
    case SDL_CONTROLLER_BUTTON_B: return SDL_SCANCODE_BACKSPACE;
    case SDL_CONTROLLER_BUTTON_X: return SDL_SCANCODE_SPACE;   // (the mod menu: switch a mod on or off)
    case SDL_CONTROLLER_BUTTON_LEFTSHOULDER: return SDL_SCANCODE_Q;
    case SDL_CONTROLLER_BUTTON_RIGHTSHOULDER: return SDL_SCANCODE_E;
    case SDL_CONTROLLER_BUTTON_BACK: return SDL_SCANCODE_TAB;
    case SDL_CONTROLLER_BUTTON_START: return SDL_SCANCODE_ESCAPE;   // the pause menu (it opens the mod menu too)
    case SDL_CONTROLLER_BUTTON_DPAD_LEFT: return SDL_SCANCODE_LEFT;
    case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: return SDL_SCANCODE_RIGHT;
    case SDL_CONTROLLER_BUTTON_DPAD_UP: return SDL_SCANCODE_UP;
    case SDL_CONTROLLER_BUTTON_DPAD_DOWN: return SDL_SCANCODE_DOWN;
    case SDL_CONTROLLER_BUTTON_RIGHTSTICK: return SDL_SCANCODE_Z;
    default: return 0;
    }
}
static std::deque<int> g_wheel;
static int g_clicks = 0;
static std::string g_text;
static float g_dx = 0, g_dy = 0;
static int g_testX = -1, g_testY = -1;
void Host_TestMouse(int x, int y, int clicks, int wheel) {
    g_testX = x; g_testY = y;
    g_clicks |= clicks;
    for (; wheel > 0; --wheel) g_wheel.push_back(1);
    for (; wheel < 0; ++wheel) g_wheel.push_back(-1);
}
int Host_PopClicks() { int c = g_clicks; g_clicks = 0; return c; }
std::string Host_PopText() { std::string t; t.swap(g_text); return t; }
void Host_SetRelativeMouse(bool on) {
    g_relativeRequested = on;
    const bool capture = on && g_inputFocused;
    if ((SDL_GetRelativeMouseMode() == SDL_TRUE) == capture) return;
    SDL_SetRelativeMouseMode(capture ? SDL_TRUE : SDL_FALSE);
    g_dx = g_dy = 0;
}
void Host_PopMouseDelta(float* dx, float* dy) { *dx = g_dx; *dy = g_dy; g_dx = g_dy = 0; }
int Host_PopWheel() { if (g_wheel.empty()) return 0; int w = g_wheel.front(); g_wheel.pop_front(); return w; }
bool Host_MouseDown(int b) { return (SDL_GetMouseState(nullptr, nullptr) & SDL_BUTTON(b + 1)) != 0; }
int Host_PopKey() {
    if (g_keys.empty()) return 0;
    int k = g_keys.front();
    g_keys.pop_front();
    return k;
}
void Host_SetTitle(const char* t) { SDL_SetWindowTitle(g_win, t); }
void Host_GetMouse(int* x, int* y) {
    if (g_testX >= 0) { *x = g_testX; *y = g_testY; return; }
    int wx, wy, ww, wh, dw, dh;
    SDL_GetMouseState(&wx, &wy);
    SDL_GetWindowSize(g_win, &ww, &wh);
    SDL_GL_GetDrawableSize(g_win, &dw, &dh);
    *x = ww ? wx * dw / ww : wx;
    *y = wh ? wy * dh / wh : wy;
}
bool Host_PumpEvents() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT) return false;
        if (e.type == SDL_MOUSEWHEEL) {
            const int direction = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1 : 1;
            for (int i = 0; i < std::abs(e.wheel.y); ++i) g_wheel.push_back((e.wheel.y > 0 ? 1 : -1) * direction);
        }
        if (e.type == SDL_KEYDOWN && !e.key.repeat) g_keys.push_back((int)e.key.keysym.scancode);
        if (e.type == SDL_KEYDOWN && e.key.keysym.scancode == SDL_SCANCODE_BACKSPACE) g_text += '\b';
        if (e.type == SDL_TEXTINPUT) g_text += e.text.text;
        if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button >= 1 && e.button.button <= 3) g_clicks |= 1 << (e.button.button - 1);
        if (e.type == SDL_MOUSEMOTION && SDL_GetRelativeMouseMode()) {
            int ww, wh, dw, dh;
            SDL_GetWindowSize(g_win, &ww, &wh);
            SDL_GL_GetDrawableSize(g_win, &dw, &dh);
            g_dx += ww ? (float)e.motion.xrel * dw / ww : (float)e.motion.xrel;
            g_dy += wh ? (float)e.motion.yrel * dh / wh : (float)e.motion.yrel;
        }
        if (e.type == SDL_WINDOWEVENT && e.window.event == SDL_WINDOWEVENT_SIZE_CHANGED)
            SDL_GL_GetDrawableSize(g_win, &g_w, &g_h);
        if (e.type == SDL_WINDOWEVENT && e.window.windowID == SDL_GetWindowID(g_win) &&
            (e.window.event == SDL_WINDOWEVENT_FOCUS_LOST || e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED)) {
            g_inputFocused = e.window.event == SDL_WINDOWEVENT_FOCUS_GAINED;
            Host_SetRelativeMouse(g_relativeRequested);
            g_dx = g_dy = 0;   // never turn the camera with movement from outside the focused game
        }
        if (e.type == SDL_CONTROLLERDEVICEADDED) padOpen();
        if (e.type == SDL_CONTROLLERDEVICEREMOVED && g_pad &&
            e.cdevice.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad))) {
            SDL_GameControllerClose(g_pad);
            g_pad = nullptr;
            padOpen();   // another one, if any
        }
        if (e.type == SDL_CONTROLLERBUTTONDOWN && g_pad &&
            e.cbutton.which == SDL_JoystickInstanceID(SDL_GameControllerGetJoystick(g_pad)))
            if (const int k = padKey(e.cbutton.button)) g_keys.push_back(k);
    }
    padRead();
    return true;
}
void Host_Shutdown() {
    Host_SetRelativeMouse(false);
    g_inputFocused = false;
    if (g_pad) SDL_GameControllerClose(g_pad);
    g_pad = nullptr;
    g_padState = HostPad{};
    if (g_gl) SDL_GL_DeleteContext(g_gl);
    if (g_win) SDL_DestroyWindow(g_win);
    g_gl = nullptr; g_win = nullptr;
    SDL_Quit();
}
