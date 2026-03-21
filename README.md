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
