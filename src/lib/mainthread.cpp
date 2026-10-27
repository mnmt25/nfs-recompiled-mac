#include <lib/mainthread.h>
#include <lib/thread.h>

#ifdef __APPLE__
# define NFS_SERVE_MAIN_THREAD
#endif

namespace win32
{

#ifdef NFS_SERVE_MAIN_THREAD
struct Worker
{
    void          (*body)(void*);
    void*         data;
    SDL_AtomicInt done;
};

static int runWorker(void* data)
{
    Worker* worker = static_cast<Worker*>(data);
    worker->body(worker->data);
    SDL_SetAtomicInt(&worker->done, 1);
    return 0;
}
#endif

void MainThread::run(void (*body)(void*), void* data)
{
#ifdef NFS_SERVE_MAIN_THREAD
    Worker worker = { body, data, { 0 } };
    SDL_Thread* thread = Thread::create(&runWorker, "game", &worker);
    NFS2_ASSERT(thread);
    while (!SDL_GetAtomicInt(&worker.done))
    {
        // Also runs the callbacks queued by MainThread::call()
        SDL_PumpEvents();
        SDL_Delay(1);
    }
    SDL_WaitThread(thread, nullptr);
#else
    body(data);
#endif
}

void MainThread::call(void (*callback)(void*), void* data)
{
#ifdef NFS_SERVE_MAIN_THREAD
    SDL_RunOnMainThread(callback, data, true);
#else
    callback(data);
#endif
}

bool MainThread::waitEvent(SDL_Event* event)
{
#ifdef NFS_SERVE_MAIN_THREAD
    // Only the main thread may pump; take what it has queued
    while (SDL_PeepEvents(event, 1, SDL_GETEVENT, SDL_EVENT_FIRST, SDL_EVENT_LAST) <= 0)
    {
        SDL_Delay(1);
    }
    return true;
#else
    return SDL_WaitEvent(event);
#endif
}

}
