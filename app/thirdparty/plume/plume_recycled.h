// fable2-nx (2026-11-20, the render thread's allocations): objects made and destroyed many times a frame keep their
// memory for the next one of their kind instead of going through malloc.
//
// The CPU profile of 2026-11-20 (cpu_samples_8.bin, the render thread at 110 % of a core): about 10 % in malloc, free
// and newlib's malloc lock, mostly for the textures, views and framebuffers of resolve targets and copies that are made
// again every frame (createTexture, createTextureView, createFramebuffer, ResolveTarget, DrainSlot), and the lock was
// held by the shader compiler thread (uam: 30 % of its time waiting for it) as often as not. A class that derives from
// Recycled<Self> takes its memory from a small free list of its own (its own mutex, a fixed array), and gives it back
// there; malloc only when the list is empty or full, or for a size that is not the class's own (a derived class).
#pragma once

#include <mutex>
#include <cstddef>
#include <new>

namespace plume {

template <typename T, unsigned Keep = 256>
struct Recycled {
    static void *operator new(std::size_t size) {
        if (size == sizeof(T)) {
            if (void *p = list().take())
                return p;
        }
        return ::operator new(size);
    }
    static void operator delete(void *p, std::size_t size) {
        if (!p)
            return;
        if (size == sizeof(T) && list().give(p))
            return;
        ::operator delete(p);
    }

private:
    struct List {
        // A mutex, not a spin lock: on the Switch a thread spinning at a higher priority on the holder's core would
        // never let it run. Uncontended (the render thread makes and frees nearly all of them), it is an atomic.
        std::mutex mutex;
        unsigned count = 0;
        void *items[Keep];
        void *take() {
            std::lock_guard<std::mutex> hold(mutex);
            return count ? items[--count] : nullptr;
        }
        bool give(void *p) {
            std::lock_guard<std::mutex> hold(mutex);
            if (count >= Keep)
                return false;
            items[count++] = p;
            return true;
        }
    };
    static List &list() {
        static List *l = new List;  // never destroyed: objects may still be freed while the process exits
        return *l;
    }
};

}  // namespace plume
