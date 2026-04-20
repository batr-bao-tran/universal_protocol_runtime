# UPR Transport Benchmark Report

## Comparison Method

For each payload size and traffic pattern:

- **Baseline set** = `unix_socket`, `tcp_loopback`
- **UPR-optimized set** = `local_shm`, `unix_socket_io_uring`, `tcp_loopback_io_uring`
- **Relative** = `best(UPR-optimized) / best(baseline)` by `items_per_second`

Implementation note:

- Baseline transports (`unix_socket`, `tcp_loopback`) use standard stream sockets with the same `FrameChannel` length-prefixed framing path.
- UPR-optimized transports keep the same framing contract but change the transport engine: shared-memory ring signaling (`local_shm`) or io_uring-based submission/completion (`*_io_uring`) to reduce syscall and copy overhead in favorable workloads.

## Executive Outcome

- **Request/response:** UPR-optimized transports outperform baseline at every payload size (`1.18x` to `2.55x`, geometric mean `1.52x`).
- **One-way:** mixed results; UPR-optimized wins at `256` B, but trails baseline at `64`, `1024`, `4096`, and `65536` B (geometric mean `0.93x`).

## Summary

### Request/Response: Best UPR-Optimized vs Best Baseline

| Payload | Best UPR-optimized | Items/s | Best baseline | Items/s | Relative |
| --- | --- | ---: | --- | ---: | ---: |
| 64 B | `local_shm` | 346,888 | `unix_socket` | 284,686 | 1.22x |
| 256 B | `unix_socket_io_uring` | 484,067 | `unix_socket` | 290,022 | 1.67x |
| 1024 B | `local_shm` | 340,646 | `unix_socket` | 289,593 | 1.18x |
| 4096 B | `local_shm` | 329,839 | `unix_socket` | 247,191 | 1.33x |
| 65536 B | `local_shm` | 161,265 | `tcp_loopback` | 63,302 | 2.55x |

### One-Way: Best UPR-Optimized vs Best Baseline

| Payload | Best UPR-optimized | Items/s | Best baseline | Items/s | Relative |
| --- | --- | ---: | --- | ---: | ---: |
| 64 B | `unix_socket_io_uring` | 842,465 | `unix_socket` | 956,215 | 0.88x |
| 256 B | `unix_socket_io_uring` | 1,246,124 | `unix_socket` | 980,643 | 1.27x |
| 1024 B | `unix_socket_io_uring` | 926,195 | `unix_socket` | 1,130,326 | 0.82x |
| 4096 B | `unix_socket_io_uring` | 648,974 | `unix_socket` | 824,367 | 0.79x |
| 65536 B | `tcp_loopback_io_uring` | 269,383 | `tcp_loopback` | 281,580 | 0.96x |

## Full Transport Matrices (Items/s)

### One-Way

| Payload | `unix_socket` | `local_shm` | `tcp_loopback` | `unix_socket_io_uring` | `tcp_loopback_io_uring` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 B | 956,215 | 255,910 | 223,486 | 842,465 | - |
| 256 B | 980,643 | 261,974 | 357,786 | 1,246,124 | - |
| 1024 B | 1,130,326 | 263,625 | 689,103 | 926,195 | 536,772 |
| 4096 B | 824,367 | 243,139 | 664,046 | 648,974 | 447,084 |
| 65536 B | 147,080 | 187,182 | 281,580 | 231,562 | 269,383 |

### Request/Response

| Payload | `unix_socket` | `local_shm` | `tcp_loopback` | `unix_socket_io_uring` | `tcp_loopback_io_uring` |
| --- | ---: | ---: | ---: | ---: | ---: |
| 64 B | 284,686 | 346,888 | 131,812 | 270,713 | - |
| 256 B | 290,022 | 347,780 | 139,148 | 484,067 | - |
| 1024 B | 289,593 | 340,646 | 136,663 | 273,403 | 134,773 |
| 4096 B | 247,191 | 329,839 | 130,048 | 220,391 | 168,902 |
| 65536 B | 59,240 | 161,265 | 63,302 | 97,761 | 63,493 |


## Practical Guidance

- If the workload is request/response heavy, use UPR-optimized backends (`local_shm` first, with targeted `unix_socket_io_uring` for strong 256 B profiles).
- If the workload is one-way streaming, keep baseline socket transports as default, and enable `io_uring` only where profile-specific wins are confirmed.
- Keep selection policy payload-aware; the fastest backend changes with frame size.

## Reproduce

```bash
bazel build -c opt //packages/upr_adapters:upr_transport_benchmark

taskset -c 0 ./bazel-bin/packages/upr_adapters/upr_transport_benchmark \
  --benchmark_repetitions=50 --benchmark_report_aggregates_only=true \
  --benchmark_out=/tmp/upr_transport_benchmarks.json --benchmark_out_format=json
```
