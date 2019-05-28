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

/*

This file introduces std::atomic_wait, atomic_notify_one, atomic_notify_all.

Strategies:
 * Contention table. Used to optimize futex notify, or to hold CVs. Disable with __NO_TABLE.
 * Futex. Supported on Linux and Windows. Requires table on Linux. Disable with __NO_FUTEX.
 * Condition variables. Supported on Linux and Mac. Requires table. Disable with __NO_CONDVAR.
 * Timed back-off. Supported on everything. Disable with __NO_SLEEP.
 * Spinlock. Supported on everything. Force with __NO_IDENT.

Strategy selection by platform:
 * Linux: default to futex (table), fallback to CVs -> timed backoff -> spin.
 * Mac: default to CVs (table), fallback to timed backoff -> spin.
 * Windows: default to futex (no table), fallback to timed backoff -> spin.
 * CUDA: default to timed backoff, fallback to spin.
 * Unidentified: default to spin.

*/

//#define __NO_TABLE
//#define __NO_FUTEX
//#define __NO_CONDVAR
//#define __NO_SLEEP
//#define __NO_IDENT

// To benchmark against spinning
//#define __NO_WAIT

///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////
///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////
///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////
///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////
///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////
///////////// IMPLEMENTATION /////////////////////////////////////////////////////////////////

#include <cstdint>
#include <climits>
#include <cassert>
#include <type_traits>

#if defined(__NO_IDENT)

    #define __ABI
    #define __YIELD()
    #define __SLEEP(x)
    #define __YIELD_PROCESSOR()

#else

#if defined(__CUSTD__)
    #define __NO_FUTEX
    #define __NO_CONDVAR
    #ifndef __CUDACC__
        #define __host__ 
        #define __device__
    #endif
    #define __ABI __host__ __device__
#else
    #define __ABI
#endif

#if defined(__APPLE__) || defined(__linux__)

    #include <unistd.h>
    #include <sched.h>
    #define __YIELD() sched_yield()
    #define __SLEEP(x) usleep(x)

    #if defined(__aarch64__)
        #  define __YIELD_PROCESSOR() asm volatile ("yield" ::: "memory")
    #elif defined(__x86_64__)
        # define __YIELD_PROCESSOR() asm volatile ("pause" ::: "memory")
    #elif defined (__powerpc__)
        # define __YIELD_PROCESSOR() asm volatile ("or 27,27,27" ::: "memory")
    #endif
#endif

#if defined(__linux__) && !defined(__NO_FUTEX)

    #if !defined(__NO_TABLE)
        #define __TABLE
    #endif

    #include <time.h>
    #include <unistd.h>
    #include <linux/futex.h>
    #include <sys/syscall.h>
    
    #define __FUTEX
    #define __FUTEX_TIMED
    #define __type_used_directly(_T) (std::is_same<typename std::remove_const< \
            typename std::remove_volatile<_Tp>::type>::type, __futex_preferred_t>::value)
    using __futex_preferred_t = std::int32_t;
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __do_direct_wait(_Tp const* ptr, _Tp val, void const* timeout) {
        syscall(SYS_futex, ptr, FUTEX_WAIT_PRIVATE, val, timeout, 0, 0);
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __do_direct_wake(_Tp const* ptr, bool all) {
        syscall(SYS_futex, ptr, FUTEX_WAKE_PRIVATE, all ? INT_MAX : 1, 0, 0, 0);
    }

#elif defined(_WIN32) && !defined(__CUSTD__)

    #define __NO_CONDVAR
    #define __NO_TABLE

    #include <Windows.h>
    #define __YIELD() Sleep(0)
    #define __SLEEP(x) Sleep(x)
    #define __YIELD_PROCESSOR() YieldProcessor()

    #include <intrin.h>
    template <class _Tp>
    auto __atomic_load_n(_Tp const* a, int) -> decltype(*a) {
        auto const t = *a;
        _ReadWriteBarrier();
        return t;
    }
    #define __builtin_expect(e, v) (e)

    #if defined(_WIN32_WINNT) && (_WIN32_WINNT >= _WIN32_WINNT_WIN8) && !defined(__NO_FUTEX)

        #define __FUTEX
        #define __type_used_directly(_T) (sizeof(_T) <= 8)
        using __futex_preferred_t = std::int64_t;
        template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
        void __do_direct_wait(_Tp const* ptr, _Tp val, void const*) {
            WaitOnAddress((PVOID)ptr, (PVOID)&val, sizeof(_Tp), INFINITE);
        }
        template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
        void __do_direct_wake(_Tp const* ptr, bool all) {
            if (all)
                WakeByAddressAll((PVOID)ptr);
            else
                WakeByAddressSingle((PVOID)ptr);
        }

    #endif
#endif // _WIN32

#if !defined(__FUTEX) && !defined(__NO_CONDVAR)

    #if defined(__NO_TABLE)
        #warning "Condvars always generate a table (ignoring __NO_TABLE)."
    #endif
    #include <pthread.h>
    #define __CONDVAR
    #define __TABLE
#endif

#endif // __NO_IDENT

#ifdef __TABLE
    struct alignas(64) contended_t {
    #if defined(__FUTEX)
        int                     waiters = 0;
        __futex_preferred_t     version = 0;
    #elif defined(__CONDVAR)
        int                     credit = 0;
        pthread_mutex_t         mutex = PTHREAD_MUTEX_INITIALIZER;
        pthread_cond_t          condvar = PTHREAD_COND_INITIALIZER;
    #else
        #error ""
    #endif
    };
    contended_t contention[256];
    contended_t * __contention(volatile void const * p) {
        return contention + ((uintptr_t)p & 255);
    }
#endif // __TABLE

#if defined(__CONDVAR)

    template <class _Tp>
    void __cxx_atomic_notify_all(volatile _Tp const* ptr) {
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if(__builtin_expect(0 == __atomic_load_n(&c->credit, __ATOMIC_RELAXED), 1))
            return;
        if(0 != __atomic_exchange_n(&c->credit, 0, __ATOMIC_RELAXED)) {
            pthread_mutex_lock(&c->mutex);
            pthread_mutex_unlock(&c->mutex);
            pthread_cond_broadcast(&c->condvar);
        }
    }
    template <class _Tp>
    void __cxx_atomic_notify_one(volatile _Tp const* ptr) {
        __cxx_atomic_notify_all(ptr);
    }
    template <class _Tp>
    void __cxx_atomic_try_wait_slow(volatile _Tp const* ptr, _Tp const val, int order) {
        auto * const c = __contention(ptr);
        pthread_mutex_lock(&c->mutex);
        __atomic_store_n(&c->credit, 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (val == __atomic_load_n(ptr, order))
            pthread_cond_wait(&c->condvar, &c->mutex);
        pthread_mutex_unlock(&c->mutex);
    }

#elif defined(__FUTEX)

    #if defined(__TABLE)

        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_notify_all(_Tp const* ptr) {
            auto * const c = __contention(ptr);
            __atomic_fetch_add(&c->version, 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            if (0 != __atomic_exchange_n(&c->waiters, 0, __ATOMIC_RELAXED))
                __do_direct_wake(&c->version, true);
        }
        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_notify_one(_Tp const* ptr) {
            __cxx_atomic_notify_all(ptr);
        }
        template <class _Tp, typename std::enable_if<!__type_used_directly(_Tp), int>::type = 1>
        void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp const val, int order) {
            auto * const c = __contention(ptr);
            __atomic_store_n(&c->waiters, 1, __ATOMIC_RELAXED);
            __atomic_thread_fence(__ATOMIC_SEQ_CST);
            auto const version = __atomic_load_n(&c->version, __ATOMIC_RELAXED);
            if (__builtin_expect(val != __atomic_load_n(ptr, order), 1))
                return;
        #ifdef __FUTEX_TIMED
            constexpr timespec timeout = { 2, 0 }; // Hedge on rare 'int version' aliasing.
            __do_direct_wait(&c->version, version, &timeout);
        #else
            __do_direct_wait(&c->version, version, nullptr);
        #endif
        }

    #endif // __TABLE

    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp val, int order) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_fetch_add(&c->waiters, 1, __ATOMIC_RELAXED);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
    #endif
        __do_direct_wait(ptr, val, nullptr);
    #ifdef __TABLE
        __atomic_fetch_sub(&c->waiters, 1, __ATOMIC_RELAXED);
    #endif
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_notify_all(_Tp const* ptr) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (0 != __atomic_load_n(&c->waiters, __ATOMIC_RELAXED))
    #endif
            __do_direct_wake(ptr, true);
    }
    template <class _Tp, typename std::enable_if<__type_used_directly(_Tp), int>::type = 1>
    void __cxx_atomic_notify_one(_Tp const* ptr) {
    #ifdef __TABLE
        auto * const c = __contention(ptr);
        __atomic_thread_fence(__ATOMIC_SEQ_CST);
        if (0 != __atomic_load_n(&c->waiters, __ATOMIC_RELAXED))
    #endif
            __do_direct_wake(ptr, false);
    }

#else // __FUTEX || __CONDVAR

    template <class _Tp>
    __ABI void __cxx_atomic_try_wait_slow(_Tp const* ptr, _Tp val, int order) {
    #ifndef __NO_SLEEP
        long history = 10;
        do {
            __SLEEP(history >> 2);
            history += history >> 2;
            if (history > (1 << 10))
                history = 1 << 10;
        } while (__atomic_load_n(ptr, order) == val);
    #else
        __YIELD();
    #endif
    }
    template <class _Tp>
    __ABI void __cxx_atomic_notify_one(_Tp const* ptr) {
    }
    template <class _Tp>
    __ABI void __cxx_atomic_notify_all(_Tp const* ptr) {
    }

#endif // __FUTEX || __CONDVAR

template <class _Tp>
__ABI void __cxx_atomic_wait(_Tp const* ptr, _Tp const val, int order) {
    for(int i = 0; i < 16; ++i)
        if(__builtin_expect(val != __atomic_load_n(ptr, order),1))
            return;
        else if(i < 8)
            __YIELD_PROCESSOR();
        else if(i < 16)
            __YIELD();
    while(val == __atomic_load_n(ptr, order))
        __cxx_atomic_try_wait_slow(ptr, val, order);
}

#include <atomic>

namespace std {

    template <class _Tp>
    __ABI void atomic_wait(atomic<_Tp> const& a, _Tp val, 
                           std::memory_order order = std::memory_order_seq_cst) {

        __cxx_atomic_wait((const _Tp*)&a, val, (int)order);
    }

    template <class _Tp>
    __ABI void atomic_notify_one(atomic<_Tp> const& a) {
        __cxx_atomic_notify_one((const _Tp*)&a);
    }

    template <class _Tp>
    __ABI void atomic_notify_all(atomic<_Tp> const& a) {
        __cxx_atomic_notify_all((const _Tp*)&a);
    }
}

///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////
///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////
///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////
///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////
///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////
///////////// TEST VEHICLE /////////////////////////////////////////////////////////////////

#include <mutex>
#include <thread>
#include <vector>
#include <iostream>
#include <chrono>

struct mutex {
	void lock() noexcept {
		while (1 == l.exchange(1, std::memory_order_acquire))
#ifndef __NO_WAIT
			atomic_wait(l, 1, std::memory_order_relaxed)
#endif
            ;
	}
	void unlock() noexcept {
		l.store(0, std::memory_order_release);
#ifndef __NO_WAIT
		atomic_notify_one(l);
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
            atomic_wait(out, now, std::memory_order_relaxed);
#endif
        }
	}
	void unlock() noexcept {
		out.fetch_add(1, std::memory_order_release);
#ifndef __NO_WAIT
		atomic_notify_all(out);
#endif
	}
	alignas(64) std::atomic<int> in = ATOMIC_VAR_INIT(0);
    alignas(64) std::atomic<int> out = ATOMIC_VAR_INIT(0);
};

struct barrier {
    barrier(uint32_t count) : arrived(count), expected(count) { 
    }
	void arrive_and_wait() noexcept {
        auto const old_phase = phase.load(std::memory_order_relaxed);
        auto const result = arrived.fetch_sub(1, std::memory_order_release) - 1;
        if(0 == result) {
            arrived.store(expected, std::memory_order_relaxed);
            phase.store(old_phase + 1, std::memory_order_release);
#ifndef __NO_WAIT
            atomic_notify_all(phase);
#endif
        }
        else
            while(old_phase == phase.load(std::memory_order_acquire))
#ifndef __NO_WAIT
            atomic_wait(phase, old_phase, std::memory_order_relaxed)
#endif
            ;
	}
    alignas(64) std::atomic<unsigned> phase = ATOMIC_VAR_INIT(0);
    alignas(64) std::atomic<unsigned> arrived = ATOMIC_VAR_INIT(0);
	uint32_t const expected;
};

template <class F>
void test(std::string const& name, int threads, F && f) {

	auto const t1 = std::chrono::steady_clock::now();

	int sections = 1 << 20;

	std::vector<std::thread> ts(threads);
	for (auto& t : ts)
		t = std::thread([=]() {
            f(sections / threads);
        });

	for (auto& t : ts)
		t.join();

	auto const t2 = std::chrono::steady_clock::now();

	double const d = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
	std::cout << name << " : " << d / sections << "ns per section." << std::endl;
}

#ifndef NOMAIN

int main() {

    int const max = std::thread::hardware_concurrency();
    std::cout << "System has " << max << " hardware threads." << std::endl;

    std::vector<std::pair<int, std::string>> const counts = 
        { { 1, "1 thread" }, 
          { 2, "2 threads" },
          { max, "full occupancy" },
          { max * 2, "double occupancy" } };

    for(auto const& c : counts) {
        mutex m;
        auto f = [&](int n) {
            for (int i = 0; i < n; ++i) {
                m.lock();
                m.unlock();
            }
        };
	    test("Spinlock: " + c.second, c.first, f);
    }

    for(auto const& c : counts) {
        ticket_mutex t;
        auto g = [&](int n) {
            for (int i = 0; i < n; ++i) {
                t.lock();
                t.unlock();
            }
        };
	    test("Ticket: " + c.second, c.first, g);
    }

    for(auto const& c : counts) {
        barrier b(c.first);
        auto h = [&](int n) {
            for (int i = 0; i < n; ++i)
                b.arrive_and_wait();
        };
        test("Barrier: " + c.second, c.first, h);
    }

	return 0;
}

#endif

