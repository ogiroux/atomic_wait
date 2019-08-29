# Sample implementation of C++20 synchronization facilities

This repository contains a sample implementation of these http://wg21.link/p1135 facilities:

* `atomic_wait` / `_notify` free-functions (not the member functions)
* `counting_` / `binary_semaphore`
* `latch` and `barrier`

## How do I build the sample?

This project is self-contained.

```
git clone https://github.com/ogiroux/atomic_wait
build.sh
```

## What platforms are supported?

Linux, Mac and Windows.

## How are `atomic_wait` / `_notify` composed?

The implementation has a variety of strategies that it selects by platform:
 * Contention state table. Optimizes futex usage, or holds CVs, unless `-D__NO_TABLE`.
 * Futex. Supported on Linux and Windows, unless `-D__NO_FUTEX`. Requires a table on Linux.
 * Condition variables. Supported on Linux and Mac, unless `-D__NO_CONDVAR`. Requires a table.
 * Timed back-off. Supported on everything, unless `-D__NO_SLEEP`.
 * Spinlock. Supported on everything, only used last unless `-D__NO_IDENT`.

These strategies are selected for each platform, in the order written, based on what's disabled with the macros:
 * Linux: futex + table -> CVs + table -> timed backoff -> spin.
 * Mac: CVs + table -> timed backoff -> spin.
 * Windows: futex -> timed backoff -> spin.
 * CUDA: timed backoff -> spin.
 * Unidentified platform: spin.

## How do `counting_` / `binary_semaphore` work?

The implementation has these specializations:

* The fully general case, for `counting_semaphore` instantiated for huge numbers. This is implemented in terms of `atomic<ptrdiff_t>`, `atomic_wait` / `_notify`. This path is always enabled.
* The constrained case, the default range, for numbers supported by the underlying platform semaphore (typically a `long`). This is implemented in terms of POSIX, Dispatch and Win32 semaphores, with optimizations below. Disable this path with `-D__NO_SEM`.
* The case of a unit range, such as the alias `binary_semaphore`. This is specialized only when platform semaphores are disabled. This path uses `atomic<ptrdiff_t>`, `atomic_wait` / `_notify`.

Platform semaphores get (because they need) some additional optimizations, in up two orthogonal directions:

* Front buffering: an `atomic` object models the semaphore's conceptual count (incl. negative values). Operations to the platform semaphore are avoided as long as the modeled count stays positive. This is enabled by default on all platforms, but can be disabled with `-D__NO_SEM_FRONT`.
* Back buffering: when the platform semaphore does not natively support the `release( count )` operation, an `atomic` object distributes the `release(1)` cooperatively among all threads waiting on the semaphore, as in a binary tree. This is used by default on Linux and Mac OS X, and can be disabled with `-D__NO_SEM_BACK`.

## What about `latch` and `barrier`?

At the moment, they are only implemented in terms of `atomic` operations. These aren't ready for review.
