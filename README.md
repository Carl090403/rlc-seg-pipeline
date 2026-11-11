# rlc-seg-pipeline — multi-threaded segmentation pipeline in C11

A learning project in **C11 + POSIX threads** that models a simplified radio-link-layer data path:
SDUs are segmented into PDUs that fit a transmission grant, serialised, delivered **out of order**,
parsed, reassembled and checked with CRC-32 — by a pool of worker threads fed through a bounded
multi-producer/multi-consumer queue.

> It is a simplified model inspired by RLC segmentation, **not** a 3GPP-conformant implementation.

## Contents

| File | Purpose |
|---|---|
| `src/crc32.c` | Table-driven CRC-32 (IEEE), table built once with `pthread_once` |
| `src/segmenter.c` | Segmentation, big-endian (de)serialisation of a 6-byte header, reassembly with a byte-level receive map that tolerates reordering and duplicates and rejects conflicting data |
| `src/bqueue.c` | Bounded MPMC queue (mutex + 2 condition variables) with clean shutdown semantics (`close` then drain), low-water producer wake-up and a short consumer spin before sleeping |
| `src/pipeline.c` | Producers → queue → N workers; per-worker statistics merged after join |
| `tests/test_main.c` | 35 checks: CRC known vector, segment boundaries, malformed input, reassembly, queue shutdown, end-to-end determinism across thread counts |
| `bench/bench.c` | Throughput by worker count (one untimed warm-up run first) |

## Build and test

```bash
make test     # -Wall -Wextra -Wpedantic -Wconversion -Werror
make asan     # AddressSanitizer + UBSan
make tsan     # ThreadSanitizer (data races)
make bench
```

CI (GitHub Actions) runs all of the above with **gcc and clang**.

## Performance investigation

`make bench` on a 16-thread laptop, WSL2 Ubuntu, 2 producers, 20,000 SDUs of 1,500 bytes,
120-byte payloads (stable across repeated runs and with 200,000 SDUs):

| workers | before (SDU/s) | after (SDU/s) |
|---|---|---|
| 1 | ~22k | ~65k |
| 2 | ~90k | ~125k |
| 4 | ~45k | ~145k |
| 8 | ~22k | ~140k |

Whole benchmark: 3.3 s → 1.1 s wall time, kernel time 4.3 s → 1.0 s, voluntary context
switches ~75,000 → ~3,000.

**How it was diagnosed**

1. A warm-up run changed nothing, so it was not a cold-start effect.
2. `gprof`: ~25 µs of CPU per SDU (half in `crc32`), so one worker *should* reach ~70k SDU/s.
3. `/usr/bin/time`: the run spent more time in the kernel than in user code, with one context
   switch per SDU — the cost was thread sleep/wake (futex + cross-core wake-up, expensive in a VM),
   not computation. Pinning to 4 cores with `taskset` confirmed the code itself could scale.

**What was changed**

- **Producer side (worker-bound case):** the queue is full, so every `pop` used to wake a sleeping
  producer. Producers are now woken only when the queue drains to half capacity, so one wake-up
  admits ~32 pushes. Waiter counters skip signals when nobody is blocked.
- **Consumer side (producer-bound case):** idle workers used to sleep and be woken for every single
  item. They now spin briefly (`sched_yield`, bounded) before blocking, since the queue usually
  refills within microseconds.
- Worker statistics are kept per thread and merged after `join` (no shared lock on the hot path).

Throughput now plateaus at ~145k SDU/s from 4 workers: that is the rate 2 producers can generate
SDUs, i.e. the pipeline is producer-bound. The spin trades a little idle CPU for latency — a
deliberate choice for a throughput-oriented data path.

## Design notes

- **Determinism:** SDU content depends only on `(seed, SN)`, so the end-to-end checksum is identical
  whatever the number of threads; the test asserts it.
- **No shared mutable state in workers** other than the queue; statistics are per worker.
- **Defensive parsing:** unknown flag bits, truncated buffers, mismatched SN and conflicting
  duplicates are rejected, not silently accepted.

## Honest limits and next steps

1. Replace per-SDU `malloc` with a fixed memory pool (no allocation on the hot path).
2. Lock-free ring buffer per worker, batched pops and a faster CRC (slicing-by-8) — `crc32` is now
   the top function in the profile; measure with `perf`.
3. Sequence-number windowing and timer-based discard, as a real link layer needs.
4. Bazel build alongside Make, and a Jenkins/Gerrit-style pre-merge check.
