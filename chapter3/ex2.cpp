#include <atomic>
#include <iostream>
#include <memory>
#include <mutex>
#include <new>
#include <stack>
#include <thread>
#include <vector>

/*
Bài 2  — Thread-safe Stack
Viết ThreadSafeStack<T> với interface không có interface-level race:

void push(T val)
std::shared_ptr<T> pop() — throw EmptyStack nếu rỗng
bool empty() const

Test: nhiều producer push, nhiều consumer pop. Không mất data, không crash.
*/
template <typename T>
class ThreadSafeStack {
   private:
    std::stack<T> m_stack;
    std::mutex m_lock; 
   public:
    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> g(m_lock);
        std::shared_ptr<T> top_value;
        if (m_stack.empty()) {
            return nullptr;
        }
        try {
            top_value = std::make_shared<T>(m_stack.top());  
        } catch (const std::bad_alloc& e) {
            std::cout << "Allocation failed: " << e.what() << "\n"; 
        }
        m_stack.pop();
        return top_value;
    }
    void push(T val) {
        std::lock_guard<std::mutex> g(m_lock);
        m_stack.push(val);
    }
    bool empty() {
        std::lock_guard<std::mutex> g(m_lock);
        return m_stack.empty();
    }
};

int main() {
    ThreadSafeStack<int> stack;
    const int NUM_PRODUCERS = 4;
    const int NUM_CONSUMERS = 4;
    const int ITEMS_PER_PRODUCER = 1000;
    const int TOTAL_ITEMS = NUM_PRODUCERS * ITEMS_PER_PRODUCER;

    std::atomic<int> pop_count{0};
    std::atomic<int> empty_pop_count{0};

    // Producers: each pushes ITEMS_PER_PRODUCER items
    std::vector<std::thread> producers;
    for (int i = 0; i < NUM_PRODUCERS; ++i) {
        producers.emplace_back([&, i]() {
            for (int j = 0; j < ITEMS_PER_PRODUCER; ++j) {
                stack.push(i * ITEMS_PER_PRODUCER + j);
            }
        });
    }

    // Consumers: each tries to pop until total items consumed
    std::vector<std::thread> consumers;
    for (int i = 0; i < NUM_CONSUMERS; ++i) {
        consumers.emplace_back([&]() {
            while (pop_count.load() < TOTAL_ITEMS) {
                if (!stack.empty()) {
                    auto val = stack.pop();
                    if (val) {
                        pop_count.fetch_add(1);
                    } else {
                        empty_pop_count.fetch_add(1);
                    }
                }
            }
        });
    }

    for (auto& t : producers) t.join();
    for (auto& t : consumers) t.join();

    std::cout << "Items pushed:    " << TOTAL_ITEMS << "\n";
    std::cout << "Items popped:    " << pop_count.load() << "\n";
    std::cout << "Failed pops:     " << empty_pop_count.load() << "\n";
    std::cout << "Stack empty:     " << (stack.empty() ? "yes" : "no") << "\n";

    bool pass = (pop_count.load() == TOTAL_ITEMS) && stack.empty();
    std::cout << "\nResult: " << (pass ? "PASS" : "FAIL") << "\n";

    return pass ? 0 : 1;
}
