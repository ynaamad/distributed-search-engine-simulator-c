# Distributed Search Engine Simulator (C)

A high-performance discrete event simulator for distributed search engine clusters, written in C. Models physical components (CPU, RAM, Disk), logical components (Index, Shard, Worker), and request processing (GET, PUT, SEARCH) with a priority-queue-based event loop.

This is a C port of the [original Python simulator](https://github.com/amazon-science/Distributed-Search-Engine-Simulator), and typically runs ~50-70x faster (relative to CPython).
See [NOTICE](NOTICE) for attribution.

## Building

Requires CMake 3.20+ and a C23-capable compiler (GCC 13+ or Clang 16+).

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

The binary is `build/dse_sim`.

## Usage

```bash
./build/dse_sim --workload path/to/workload.jsonl [options]
```

### Required

- `--workload PATH` — JSONL workload file (see format below), **or**
- `--rate F` / `--rate-mean F` — Poisson arrival rate for synthetic workload generation (requires `--end-time`)

### Common options

| Option | Default | Description |
|--------|---------|-------------|
| `--num-workers N` | 2 | Number of worker nodes |
| `--half-ocus N` | 2 | Half-OCUs per worker (1 half-OCU = 1 CPU core per 2 units) |
| `--queue-length N` | 100 | Max request queue depth per worker |
| `--indices A,B` | `default` | Comma-separated index names |
| `--shards N,N` | `6` | Shards per index (per worker) |
| `--profiler-output PATH` | stdout | CSV output file for metrics |
| `--profiler-period F` | 300 | Profiling interval (simulation seconds) |
| `--profiler-first F` | 0 | Delay before first profiling |
| `--end-time F` | — | Simulation end time (for synthetic workloads) |

### Synthetic workload options

| Option | Default | Description |
|--------|---------|-------------|
| `--rate F` | — | Constant Poisson arrival rate (req/s) |
| `--rate-mean F` | — | Mean rate for sinusoidal pattern |
| `--rate-period F` | — | Period of sinusoidal rate variation |
| `--cpu-mean F` | 6.2e7 | Mean CPU cycles per request |
| `--cpu-std F` | 4e7 | Std deviation of CPU cycles |
| `--mem-mean F` | 8e9 | Mean memory bytes per request |
| `--mem-std F` | 1e8 | Std deviation of memory bytes |

### Examples

Run with a JSONL workload file:
```bash
./build/dse_sim \
  --num-workers 2 --half-ocus 4 \
  --indices hello,world --shards 6,6 \
  --workload workload.jsonl \
  --profiler-output metrics.csv --profiler-period 60
```

Run with synthetic Poisson workload:
```bash
./build/dse_sim \
  --num-workers 3 --half-ocus 4 \
  --indices myindex --shards 12 \
  --rate 50 --end-time 3600 \
  --profiler-output metrics.csv --profiler-period 300
```

## Workload format (JSONL)

One JSON object per line. Three request types:

**GET** — read a document:
```json
{"type": "GET", "time": 1.5, "index": "hello", "document_id": 42, "cpu_size": 1e7, "mem_size": 1e6, "given_hash": []}
```

**PUT** — write a document:
```json
{"type": "PUT", "time": 2.0, "index": "hello", "document_id": 99, "cpu_size": 2e7, "mem_size": 2e6, "given_hash": []}
```

**SEARCH** — query across shards:
```json
{"type": "SEARCH", "time": 3.0, "index": "hello", "cpu_size_query": 1e6, "mem_size_query": 1e5, "cpu_size_response": 1e7, "mem_size_response": 1e6, "cpu_size_collate": 2e7, "given_hashes": []}
```

Fields:
- `time` — arrival time in simulation seconds
- `cpu_size` — CPU cycles required
- `mem_size` — memory bytes consumed during processing
- `given_hash` / `given_hashes` — pre-computed shard routing hashes (empty = compute from document_id)

## Profiler output (CSV)

```
time,actor,metric,value
300.0000,DSECollection(dse),num_workers,2.0000
300.0000,CPU(1),utilization_frac(1),0.4500
300.0000,Worker(1),total_requests,5000.0000
300.0000,Index(1),latency,0.0120
```

## License

This project is licensed under [CC-BY-NC-4.0](LICENSE). See [NOTICE](NOTICE) for attribution details.
