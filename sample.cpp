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

#include "sample.hpp"

static constexpr int sections = 1 << 20;

template <class F>
void test_body(int threads, F && f) {

	std::vector<std::thread> ts(threads);
	for (auto& t : ts)
		t = std::thread([=]() {
            f(sections / threads);
        });

	for (auto& t : ts)
		t.join();
}

template <class F>
void test_omp_body(int threads, F && f) {
#ifdef _OPENMP
    #pragma omp parallel num_threads(threads)
    {
        f(sections / 16);
    }
#else
    assert(0);
#endif
}

template <class F>
void test(std::string const& name, int threads, F && f, bool use_omp = false) {

	auto const t1 = std::chrono::steady_clock::now();

    if(use_omp)
        test_omp_body(threads, f);
    else
        test_body(threads, f);

	auto const t2 = std::chrono::steady_clock::now();

	double const d = double(std::chrono::duration_cast<std::chrono::nanoseconds>(t2 - t1).count());
	std::cout << name << " : " << d / sections << "ns per section." << std::endl;
}

int main() {

    int const max = std::thread::hardware_concurrency();
    std::cout << "System has " << max << " hardware threads." << std::endl;

    std::vector<std::pair<int, std::string>> const counts = 
        { { 1, "1 thread" }, 
          { 2, "2 threads" },
          { max, "full occupancy" },
          { max * 2, "double occupancy" } };

#ifndef __NO_MUTEX

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
        sem_mutex t;
        auto g = [&](int n) {
            for (int i = 0; i < n; ++i) {
                t.lock();
                t.unlock();
            }
        };
	    test("Semlock: " + c.second, c.first, g);
    }

#endif

#ifndef __NO_BARRIER

    for(auto const& c : counts) {
        barrier b(c.first);
        auto h = [&](int n) {
            for (int i = 0; i < n; ++i)
                b.arrive_and_wait();
        };
        test("Barrier: " + c.second, c.first, h);

#if defined(_POSIX_THREADS) && !defined(__APPLE__)
        pthread_barrier_t pb;
        pthread_barrier_init(&pb, nullptr, c.first);
        auto i = [&](int n) {
            for (int i = 0; i < n; ++i)
                pthread_barrier_wait(&pb);
        };
        test("Pthread: " + c.second, c.first, i);
        pthread_barrier_destroy(&pb);
#endif

#ifdef _OPENMP
        auto o = [&](int n) {
            for (int i = 0; i < n; ++i) {
                #pragma omp barrier
            }
        };
	    test("OMP: " + c.second, c.first, o, true);
#endif
    }

#endif

	return 0;
}
