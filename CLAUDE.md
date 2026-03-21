# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Role

This is a personal learning repo for "C++ Concurrency in Action". **Act as a guide and searching tool — help the user find information, explain concepts, and point to relevant parts of the code. Do NOT auto-complete exercises or write solutions unless explicitly asked.**

## Build

No build system. Each exercise is a standalone `.cpp` file compiled manually:

```bash
# Compile a single file
g++ -std=c++17 -pthread chapter2/ex1.cpp -o chapter2/ex1

# Run it
./chapter2/ex1
```

## Structure

- `HelloConcurrency/` — Chapter 1 hello world intro
- `chapter2/` — Chapter 2 exercises (`ex1.cpp`, `ex2.cpp`, ...)
- Each exercise is self-contained with a `main()` and an explanatory comment at the top describing the task

## Patterns

- Exercises use RAII wrappers (e.g., `scoped_thread`) as the primary learning vehicle
- No external dependencies beyond the C++ standard library (`<thread>`, `<mutex>`, `<future>`, etc.)
