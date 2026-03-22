#include <iostream>
#include <stack>
#include <thread>

/*
Bài 2  — Thread-safe Stack
Viết ThreadSafeStack<T> với interface không có interface-level race:

void push(T val)
std::shared_ptr<T> pop() — throw EmptyStack nếu rỗng
bool empty() const

Test: nhiều producer push, nhiều consumer pop. Không mất data, không crash.
*/


