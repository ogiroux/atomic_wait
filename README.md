# Sample implementation of `std::atomic_wait`
This repository contains a sample implementation of the http://wg21.link/p1135 atomic waiting and notifying functions.

## How do I build the sample?

This project is self-contained.

```
git clone https://github.com/ogiroux/atomic_wait
build.sh
```

## What platforms are supported?

Linux, Mac and Windows. Support for CUDA is coming.

## How does it work?

The implementation has a variety of strategies:
 * Contention table. Used to optimize futex notify, or to hold CVs. Disable with `-D__NO_TABLE`.
 * Futex. Supported on Linux and Windows. Requires table on Linux. Disable with `-D__NO_FUTEX`.
 * Condition variables. Supported on Linux and Mac. Requires table. Disable with `-D__NO_CONDVAR`.
 * Timed back-off. Supported on everything. Disable with `-D__NO_SLEEP`.
 * Spinlock. Supported on everything. Force with `-D__NO_IDENT`.
 
These strategies are selected for each platform, in order:
 * Linux: default to futex (table), fallback to CVs -> timed backoff -> spin.
 * Mac: default to CVs (table), fallback to timed backoff -> spin.
 * Windows: default to futex (no table), fallback to timed backoff -> spin.
 * CUDA: default to timed backoff, fallback to spin.
 * Unidentified: default to spin.
 
