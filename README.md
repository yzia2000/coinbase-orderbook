# coinbase-orderbook

Real-time orderbook viewer for Coinbase products. Connects to the Coinbase WebSocket feed, maintains a local L2 orderbook in a flat sorted array, and renders a colorized TUI to files — viewable with `watch cat`.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue) ![Boost.Beast](https://img.shields.io/badge/WebSocket-Boost.Beast-orange)

## Architecture

```
                          IO thread                              Render thread
                    ┌─────────────────┐                     ┌──────────────────┐
Coinbase WS Feed ──►  WebSocketClient ──► Orderbook ──────────► TuiRenderer    │
  (level2_batch)    │  (Boost.Beast)   │  (flat array)  │   │  (file writer)   │
                    └─────────────────┘       │         │   └──────────────────┘
                                              ▼         │          ▼
                                      FlatBookSideBuilder   output/*.txt
                                       sorted std::array    (atomic rename)
                                              │
                                              ▼
                                     Seqlock<FlatSnapshot>
                                      (lock-free publish)
```

### Components

- **WebSocketClient**: Async SSL WebSocket via Boost.Beast, subscribes to `level2_batch` + `heartbeat` channels on `wss://ws-feed.exchange.coinbase.com`
- **Orderbook**: Maintains top-20 bid/ask levels in `FlatBookSideBuilder` — a sorted `std::array` with linear scan and shift. No `std::map`, no heap allocations on the update path
- **Seqlock**: Lock-free single-writer/multi-reader primitive. Data stored in `std::atomic<uint64_t>` words for C++ memory model correctness. Writer never blocks; reader retries on torn read
- **TuiRenderer**: Periodic file writer (250ms). Reads snapshots via seqlock (lock-free), writes ANSI-colored output with atomic tmp+rename

### Threading Model

```
Thread 1 (main)   — setup, signal wait, teardown
Thread 2 (ASIO)   — all WebSocket I/O, orderbook updates, seqlock publish
Thread 3 (render) — periodic seqlock reads, file writes
```

No mutex anywhere. The only shared state crosses threads via the seqlock — the writer (IO thread) publishes a trivially-copyable `FlatSnapshot` (~700 bytes), and the reader (render thread) loads it lock-free.

## Build

Requires [Conan 2](https://conan.io/) and CMake 3.20+.

```bash
conan install . --build=missing
cmake --preset conan-release
cmake --build --preset conan-release
```

## Usage

```bash
# Default: BTC-USD, ETH-USD, SOL-USD
./build/Release/coinbase-orderbook

# Custom instruments
./build/Release/coinbase-orderbook BTC-USD ETH-USD DOGE-USD AVAX-USD

# Custom output directory
./build/Release/coinbase-orderbook --output /tmp/books BTC-USD

# View the orderbook (in another terminal)
watch -t -n 0.3 --color cat output/BTC-USD.txt
```

## Output

Each product gets a file in the output directory with a colored orderbook display:

```
=====================================================
  BTC-USD ORDERBOOK  (updates: 4217)
=====================================================

         PRICE  SIZE              DEPTH
-----------------------------------------------------
     67842.31  0.00120000        #
     67841.99  0.05340000        ######
     67841.50  0.12000000        ##############
                SPREAD: 0.49 (0.0007%)
     67841.01  0.08500000        ##########
     67840.88  0.03200000        ####
     67840.10  0.00500000        #
=====================================================
```

## Tests

```bash
./build/Release/orderbook-test
```

34 tests covering:
- **FlatBookSideBuilder**: insert ordering, update-in-place, removal, eviction at capacity, discard of out-of-range levels
- **Orderbook**: snapshot/update lifecycle, bid/ask sort invariants, spread consistency, update counting
- **Seqlock**: store/load correctness, concurrent stress tests (1M+ writer iterations with reader consistency checks on both small and large structs)

## Benchmarks

```bash
./build/Release/orderbook-bench
```

Flat array + seqlock vs `std::map` + seqlock (both publish `FlatSnapshot` via seqlock — the difference is the write-side data structure):

| Benchmark | Map | Flat Array | Speedup |
|-----------|-----|------------|---------|
| Write | 2,060 ns | 25 ns | ~82x |
| Read | 62 ns | 61 ns | ~same |
| Write + Publish | 2,398 ns | 198 ns | ~12x |
| Concurrent R+W | 114 ns | 128 ns | ~same |

Write speedup comes from eliminating red-black tree pointer chasing and heap allocations. Read performance is identical because both paths read the same `FlatSnapshot` through the seqlock.

## Dependencies

Managed via Conan:

| Library | Purpose |
|---------|---------|
| Boost 1.87 | Beast (WebSocket + HTTP), ASIO (async I/O) |
| OpenSSL 3.4 | TLS for secure WebSocket connection |
| nlohmann_json 3.11 | JSON parsing for Coinbase messages |
| Google Test 1.15 | Unit tests |
| Google Benchmark 1.9 | Performance benchmarks |
