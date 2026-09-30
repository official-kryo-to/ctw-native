// OS abstraction layer expected by the game code (mirrors the original OS_* API surface).
// Semantics were determined by reading the original Android implementation.
#pragma once
#include <cstdint>

enum OSFileDataArea { OS_AREA_RESOURCES = 0, OS_AREA_DOCUMENTS = 1 };
enum OSFileAccessType { OS_FILE_READ = 0, OS_FILE_WRITE = 1, OS_FILE_READWRITE = 2, OS_FILE_READ_ASYNC = 3 };
enum OSFileResult { OS_OK = 0, OS_FAIL = 1, OS_EOF = 2, OS_SEEK_ERROR = 3 };
enum OSEventType { OS_EVENT_PAUSE = 8, OS_EVENT_RESUME = 9 };

// --- application ---
// (OS_ApplicationStartup/Tick/Event belong to the game side, not the platform layer; they come with the game code port.)

// --- files ---
void OS_SetResourceRoot(const char* dir);   // where the user's extracted game data lives
void OS_SetDocumentsRoot(const char* dir);  // saves
int  OS_FileOpen(OSFileDataArea, void** out, const char* path, OSFileAccessType);
int  OS_FileRead(void* f, void* dst, int bytes);
int  OS_FileWrite(void* f, void* src, int bytes);
int  OS_FileSetPosition(void* f, int pos);
int  OS_FileGetPosition(void* f);
int  OS_FileSize(void* f);
int  OS_FileClose(void* f);
int  OS_FileDelete(OSFileDataArea, const char* path);
int  OS_FileFlush(void* f);

// --- time ---
int    OS_TimeMS();
double OS_TimeAccurate();   // seconds since startup
void   OS_ThreadSleep(int microseconds);

// --- threads / sync ---
typedef unsigned (*OSThreadFunc)(void*);
void* OS_ThreadLaunch(OSThreadFunc fn, void* arg, unsigned stack, const char* name, void* unused, int priority);
void  OS_ThreadWait(void* t);
void  OS_ThreadClose(void* t);
void* OS_MutexCreate(const char* name);
void  OS_MutexDelete(void* m);
void  OS_MutexObtain(void* m);
void  OS_MutexRelease(void* m);
void* OS_SemaphoreCreate();
void  OS_SemaphoreDelete(void* s);
void  OS_SemaphorePost(void* s);
void  OS_SemaphoreWait(void* s);
bool  OS_SemaphoreTryWait(void* s);

// --- screen ---
unsigned OS_ScreenGetWidth();
unsigned OS_ScreenGetHeight();
void     OS_ScreenSwapBuffers();
bool     OS_ShowingSplashScreen();

// --- host (not part of the original API) ---
bool Host_Init(const char* title, int w, int h);
bool Host_PumpEvents();   // returns false when the user closes the window
void Host_Shutdown();
void Host_SetTitle(const char* title);
void Host_GetMouse(int* x, int* y);   // in drawable pixels
bool Host_MouseDown(int button);      // 0 = left
int  Host_PopWheel();                 // +1 / -1 per wheel notch, 0 when none
int  Host_PopKey();       // SDL scancode of the next pressed key, or 0
