/*

Copyright (c) 2019, NVIDIA Corporation

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in
all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
THE SOFTWARE.

*/

#include <chrono>
#include <atomic>
#include <atomic_wait>
#include <semaphore>
#include <latch>
#include <barrier>

struct mutex {
	void lock() noexcept {
		while (1 == l.exchange(1, std::memory_order_acquire))
#ifndef __NO_WAIT
			atomic_wait_explicit(&l, 1, std::memory_order_relaxed)
#endif
            ;
	}
	void unlock() noexcept {
		l.store(0, std::memory_order_release);
#ifndef __NO_WAIT
		atomic_notify_one(&l);
#endif
	}
	std::atomic<int> l = ATOMIC_VAR_INIT(0);
};

struct ticket_mutex {
	void lock() noexcept {
        auto const my = in.fetch_add(1, std::memory_order_acquire);
        while(1) {
            auto const now = out.load(std::memory_order_acquire);
            if(now == my)
                return;
#ifndef __NO_WAIT
            atomic_wait_explicit(&out, now, std::memory_order_relaxed);
#endif
        }
	}
	void unlock() noexcept {
		out.fetch_add(1, std::memory_order_release);
#ifndef __NO_WAIT
		atomic_notify_all(&out);
#endif
	}
	alignas(64) std::atomic<int> in = ATOMIC_VAR_INIT(0);
    alignas(64) std::atomic<int> out = ATOMIC_VAR_INIT(0);
};

struct sem_mutex {
	void lock() noexcept {
        c.acquire();
	}
	void unlock() noexcept {
        c.release();
	}
	std::binary_semaphore c = 1;
};
