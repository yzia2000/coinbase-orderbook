# coinbase-orderbook

Real-time orderbook viewer for Coinbase products. Connects to the Coinbase WebSocket feed, maintains a local L2 orderbook, and renders a colorized TUI to files — viewable with `watch cat`.

![C++20](https://img.shields.io/badge/C%2B%2B-20-blue) ![Boost.Beast](https://img.shields.io/badge/WebSocket-Boost.Beast-orange)

## Architecture

```
Coinbase WS Feed ──► WebSocketClient ──► Orderbook (per product) ──► TuiRenderer ──► output/*.txt
   (level2_batch)      (Boost.Beast)       (std::map, mutex)         (atomic write)
```

- **WebSocketClient**: Async SSL WebSocket via Boost.Beast, subscribes to `level2_batch` + `heartbeat` channels
- **Orderbook**: Thread-safe L2 book using `std::map<double, double>` with `std::greater` for bid-side descending order. Handles full snapshots and incremental updates
- **TuiRenderer**: Periodic file writer (250ms default). Atomic writes via tmp+rename to prevent partial reads. ANSI color output (green bids, red asks, yellow spread)

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

## Dependencies

Managed via Conan:

| Library | Purpose |
|---------|---------|
| Boost 1.87 | Beast (WebSocket + HTTP), ASIO (async I/O) |
| OpenSSL 3.4 | TLS for secure WebSocket connection |
| nlohmann_json 3.11 | JSON parsing for Coinbase messages |
