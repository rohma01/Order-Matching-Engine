# Order Matching Engine

A single-threaded, low-latency C++20 limit order matching engine with a lock-free SPSC ingress queue, fixed-capacity object pools, and intrusive price-time priority queues.

## Architecture

```text
                         Producer thread
                                |
                                v
                  +---------------------------+
                  | LockFreeQueue.hpp         |
                  | Bounded SPSC ring buffer  |
                  +-------------+-------------+
                                |
                                v
                  +---------------------------+
                  | OrderBook                  |
                  |                            |
                  |  +----------------------+  |
                  |  | Bid levels           |  |
                  |  | unordered_map        |  |
                  |  +----------+-----------+  |
                  |             |              |
                  |  +----------v-----------+  |
                  |  | Limit                |  |
                  |  | head <-> Order list  |  |
                  |  +----------------------+  |
                  |                            |
                  |  +----------------------+  |
                  |  | Ask levels           |  |
                  |  | unordered_map        |  |
                  |  +----------+-----------+  |
                  |             |              |
                  |  +----------v-----------+  |
                  |  | Limit                |  |
                  |  | head <-> Order list  |  |
                  |  +----------------------+  |
                  +-------------+-------------+
                                |
                                v
                  Trade events: buy ID, sell ID,
                  quantity, and execution price

                  MemoryPool.hpp owns fixed-capacity
                  Order and Limit storage.
```

## Repository Layout

```text
.
├── CMakeLists.txt
├── main.cpp
└── include/order_matching/
    ├── LockFreeQueue.hpp
    ├── MemoryPool.hpp
    ├── Order.hpp
    └── OrderBook.hpp
```

## Low-Latency Design Decisions

- **Cache alignment:** Core `Order`, `Limit`, `ObjectPool`, `SpscRingBuffer`, and `OrderBook` structures use `alignas(64)` to reduce false sharing and improve cache-line locality.
- **Lock-free SPSC ingress:** `SpscRingBuffer<T, Capacity>` uses separate cache-aligned atomic head and tail indexes with acquire/release publication. It is intended for exactly one producer and one consumer.
- **O(1) order queue updates:** Each `Limit` owns an intrusive doubly-linked FIFO list of `Order` nodes. Appending, unlinking, and cancelling an order update neighboring pointers directly without traversing the queue.
- **Zero-allocation order storage:** `ObjectPool<T, Capacity>` pre-allocates aligned storage and reuses slots through a free-index stack. Order creation and release do not call `new`, `delete`, or `malloc`.
- **Constant-time indexes:** Reserved `std::unordered_map` instances provide direct lookup by order ID and price level. Price-time matching selects the best crossing level and consumes its FIFO head orders first.
- **Deterministic execution:** Matching emits `Trade` events containing `buyOrderId`, `sellOrderId`, `matchedQuantity`, and `executedPrice`.

## Benchmark Results

The custom benchmark generates 1,000,000 deterministic synthetic limit orders with random quantities and prices around a fixed midpoint. Results below were produced with Clang, `-O3`, and `-march=native` on the development machine.

| Metric | Result |
|---|---:|
| Requests generated | 1,000,000 |
| Requests processed | 1,000,000 |
| Requests rejected | 0 |
| Throughput | 16,904,373 req/s |
| Average latency | 59.16 ns/order |
| p50 latency | 42 ns |
| p99 latency | 167 ns |
| p99.9 latency | 917 ns |

The measured throughput exceeds 140,000 requests/sec and the p50/p99 latencies are sub-microsecond. Results vary with CPU, compiler, operating-system scheduling, and build configuration.

## Build and Run

Requirements:

- C++20 compiler
- CMake 3.20 or newer

Configure and build a Release binary:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build
```

Run the benchmark:

```bash
./build/order_matching_benchmark
```

The target explicitly enables `-O3` and `-march=native` through `CMakeLists.txt`.

## Scope

The engine is designed as a single-threaded matching core. The SPSC queue provides a lock-free boundary for one producer and one consumer; it does not make the order book itself multi-producer or multi-consumer safe.
