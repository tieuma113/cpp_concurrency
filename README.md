# cpp_concurrency

Personal learning repo following ["C++ Concurrency in Action" by Anthony Williams](https://www.manning.com/books/c-plus-plus-concurrency-in-action).

## Build

No build system. Each exercise is a standalone `.cpp` file compiled manually:

```bash
g++ -std=c++17 -pthread <file>.cpp -o <output>
./output
```

Example:

```bash
g++ -std=c++17 -pthread chapter2/ex1.cpp -o chapter2/ex1 && ./chapter2/ex1
```

## Exercises

| Chapter | File | Task | Concepts |
|---------|------|------|----------|
| 1 | `HelloConcurrency/main.cpp` | Hello concurrency | `std::thread`, `join()` |
| 2 | `chapter2/ex1.cpp` | Implement `scoped_thread` with RAII | RAII, move semantics, exception safety, deleted special members |
| 2 | `chapter2/ex2.cpp` | Thread registry — spawn N threads, collect & print IDs | `hardware_concurrency()`, `thread::id`, `vector<thread::id>` |
| 2 | `chapter2/ex3.cpp` | Parallel Transform — divide data into N chunks, multiply each by multiplier | `std::ref`, thread partitioning, `std::for_each` |
| 3 | `chapter3/ex1.cpp` | Thread-safe Counter — increment/decrement with mutex, 4 threads x 100k increments | `std::mutex`, `lock_guard` |
| 3 | `chapter3/ex2.cpp` | Thread-safe Stack — thread-safe stack with push/pop, producer-consumer test | template class, producer-consumer pattern |
| 3 | `chapter3/ex3.cpp` | Deadlock Demo + Fix — deadlock with 2 mutexes, fixed with scoped_lock and fixed lock ordering | `std::scoped_lock`, fixed lock ordering |
