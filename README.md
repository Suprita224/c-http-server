# C HTTP Server

A multithreaded HTTP/1.1 static file server written in C from scratch, using POSIX sockets and a custom thread pool. No frameworks, no external libraries.

## Features

- Serves static files (HTML, CSS, JS, images) from the `www/` folder
- Thread pool with a bounded job queue (mutex + condition variables)
- Correct status codes: 200, 403, 404, 405
- Path traversal protection (`/../` requests are blocked)
- Correct `Content-Type` for common file types
- Thread-safe access log (`access.log`)
- Graceful shutdown on Ctrl+C (SIGINT/SIGTERM)
- Configurable thread count: `./server -t 8`

## How it works

```
browser --> accept loop (main thread) --> bounded queue --> worker threads --> read request --> serve file --> log
```

The main thread only accepts connections and pushes each client socket onto a queue. A fixed pool of worker threads pops sockets off the queue and handles them. Workers sleep on a condition variable when the queue is empty, so idle threads use no CPU.

## Build and run

```bash
gcc -Wall -pthread -O2 -o server main.c
./server -t 4
```

Then open http://localhost:8080

## Tests

```bash
./server -t 4 &
python3 tests/test_server.py
```

The tests check 200/403/404/405 responses, content types, and 200 requests from 50 concurrent clients.

## Results

Benchmark: `wrk -t4 -c100 -d10s` (4 wrk threads, 100 connections, 10 seconds), serving a small HTML page.

| Server | Requests/sec | Avg latency |
|---|---|---|
| This server, 1 thread | 20,244 | 5.01 ms |
| This server, 4 threads | 38,716 | 2.44 ms |
| This server, 8 threads | 57,808 | 1.30 ms |
| Python `http.server` | 952 | 25.43 ms |

| Check | Result |
|---|---|
| AddressSanitizer | No memory errors, no leaks |
| ThreadSanitizer | No data races |

Measured on WSL2 Ubuntu with the server and wrk on the same machine, so absolute numbers vary between runs (a 4-thread run gave 35,000-39,000 req/s across several tries). The relative scaling is the point.

## Design decisions

- **Thread pool instead of a thread per connection:** a fixed number of threads avoids the cost of creating a thread for every request and caps resource use.
- **Bounded queue:** if the queue is full, the accept loop waits instead of using unlimited memory.
- **Mutex + condition variables:** workers block instead of busy-waiting.
- **Separate mutex for the log:** several threads write to one file, so writes are serialized.

## Limitations and future work

- Each connection serves one request and then closes (`Connection: close`); keep-alive would improve throughput.
- Only GET is supported.
- An `epoll` event-driven mode and `sendfile()` for zero-copy file transfer would be natural next steps.
  
## Performance

Benchmarked with [wrk](https://github.com/wg/wrk) (`wrk -t4 -c100 -d10s`, serving `index.html`) on WSL2 Ubuntu.

| Server                         | Requests/sec |
|--------------------------------|-------------:|
| This server, 1 worker thread   |      ~20,200 |
| This server, 4 worker threads  |      ~38,700 |
| This server, 8 worker threads  |      ~57,800 |
| Python `http.server` (baseline)|         ~952 |

Throughput scales with the thread pool size, and the 8-thread run is roughly 60x faster than Python's built-in server.

### Correctness checks
- Built and tested with AddressSanitizer and ThreadSanitizer, with no errors or data races reported.
- Automated end-to-end tests (Python) cover static files, MIME types, and 403/404/405 responses.

## Live stats

`GET /stats` returns server metrics as JSON, and the demo page at `/index.html` polls it every second:

    {"requests": 128, "uptime_sec": 54, "threads": 4}
