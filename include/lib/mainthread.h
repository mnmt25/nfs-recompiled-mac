#ifndef LIB_MAINTHREAD_H_
#define LIB_MAINTHREAD_H_

#include <SDL3/SDL.h>

namespace win32
{

/* Cocoa only lets the process main thread create windows and pump events,
 * but the games create their window and run their message loop on threads
 * of their own. On macOS the game therefore runs on a worker thread while
 * the main thread pumps events and serves window requests; elsewhere
 * everything stays on the calling thread. */
class MainThread
{
public:
    // Runs body(data) and returns once it is done.
    static void run(void (*body)(void*), void* data);
    // Runs callback(data) on the main thread and waits for it.
    static void call(void (*callback)(void*), void* data);
    // Blocks until an event is available, like SDL_WaitEvent().
    static bool waitEvent(SDL_Event* event);
};

}

#endif
