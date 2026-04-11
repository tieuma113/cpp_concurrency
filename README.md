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

## Structure

- `HelloConcurrency/` — Chapter 1 hello world intro
- `chapter2/` — Chapter 2 exercises (early versions)
- `chapter3/` — Chapter 3 exercises (early versions)
- `chap1-4/` — Combined exercises for chapters 2–4 (current)
- `doc/` — Exercise specifications and notes

## Exercises

### Chapter 1 — Hello Concurrency

| File | Task | Concepts |
|------|------|----------|
| `HelloConcurrency/main.cpp` | Hello concurrency | `std::thread`, `join()` |

### Chapters 2–4 (combined set in `chap1-4/`)

| File | Task | Concepts |
|------|------|----------|
| `chap1-4/ex1.cpp` | `scoped_thread` — RAII wrapper for `std::thread` | RAII, move semantics, exception safety, deleted special members |
| `chap1-4/ex2.cpp` | `parallel_accumulate` — split work across threads without `std::async` | thread partitioning, `hardware_concurrency()` |
| `chap1-4/ex3.cpp` | `threadsafe_stack<T>` — thread-safe stack with no interface race conditions | `std::mutex`, `shared_ptr`, producer-consumer pattern |
| `chap1-4/ex4.cpp` | `rw_cache<Key, Value>` — read-write cache with lazy init | `std::shared_mutex`, `shared_lock`, `unique_lock` |
| `chap1-4/ex5.cpp` | `threadsafe_queue<T>` — queue with condition variable | `std::condition_variable`, blocking/non-blocking ops |
| `chap1-4/ex6.cpp` | *(in progress)* | |

### Earlier exercises (chapters 2–3)

| File | Task | Concepts |
|------|------|----------|
| `chapter2/ex1.cpp` | Implement `scoped_thread` with RAII | RAII, move semantics, exception safety |
| `chapter2/ex2.cpp` | Thread registry — spawn N threads, collect & print IDs | `hardware_concurrency()`, `thread::id` |
| `chapter2/ex3.cpp` | Parallel Transform — divide data into N chunks | `std::ref`, thread partitioning, `std::for_each` |
| `chapter3/ex1.cpp` | Thread-safe Counter — increment/decrement with mutex | `std::mutex`, `lock_guard` |
| `chapter3/ex2.cpp` | Thread-safe Stack — push/pop with producer-consumer test | template class, producer-consumer pattern |
| `chapter3/ex3.cpp` | Deadlock Demo + Fix — 2 mutexes, fixed with scoped_lock | `std::scoped_lock`, fixed lock ordering |
