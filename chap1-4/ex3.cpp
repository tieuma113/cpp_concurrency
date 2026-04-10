// ## Bài 3 🟡 — `threadsafe_stack<T>`: Thread-safe stack không có race
// condition trong interface
//
// ### Đặc tả hành vi
//
// Viết class template `threadsafe_stack<T>` với interface:
//
// ```
// push(T value)
// pop(T& out_value)              // overload 1: ghi vào biến
// pop() -> shared_ptr<T>         // overload 2: trả về pointer
// empty() -> bool
// ```
//
// Yêu cầu về correctness:
// - `push` và `pop` từ nhiều thread đồng thời không được tạo ra data race.
// - Nếu stack rỗng và `pop` được gọi, phải ném exception `empty_stack` (tự định
// nghĩa).
// - Copy constructor phải hoạt động đúng và thread-safe: trong lúc copy, stack
// nguồn phải bị lock.
// - Copy assignment bị xóa.
//
// ### Ràng buộc cứng
//
// - Bên trong dùng `std::stack<T>` hoặc `std::deque<T>` làm storage.
// - Dùng đúng một `std::mutex`.
// - Không được để bất kỳ method nào trả về reference hoặc raw pointer trỏ vào
// element bên trong (ngoại trừ `pop()` overload 2 trả về `shared_ptr`).
// - `empty()` phải lock mutex — giải thích tại sao điều này cần thiết dù nó là
// `const`.
//
// ### Invariant cần giữ
//
// Số lần `pop` thành công không bao giờ vượt quá số lần `push` trước đó. Không
// có element nào bị pop hai lần. Không có element nào bị mất.
//
// ### Bài test bắt buộc
//
// 4 producer threads đẩy 100 giá trị mỗi thread (tổng 400). 4 consumer threads
// pop liên tục cho đến khi tổng số element đã pop = 400. Dùng
// `std::atomic<int>` để đếm. Kết quả: tổng giá trị pop ra phải bằng tổng giá
// trị đã push vào.
//
// ### Debug checkpoint
//
// - Chạy với `-fsanitize=thread`. Không được có report nào.
// - Consumer phải loop với try/catch trên `empty_stack` — tại sao không dùng
// `empty()` trước khi `pop()`?
// - Test: copy stack trong khi thread khác đang push. Kết quả có consistent
// không?
//
// ### Câu hỏi phân tích
//
// 1. Tại sao interface `top()` + `pop()` tách nhau lại inherently racy? Trace
// một interleaving cụ thể.
// 2. Tại sao `pop()` overload 2 trả về `shared_ptr<T>` thay vì `T` trực tiếp?
// Khi nào `unique_ptr` là lựa chọn tốt hơn?
// 3. `empty()` phải lock mutex — điều này không phá vỡ `const` semantics không?
// Giải thích vai trò của `mutable`.
//
#include <algorithm>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stack>
#include <thread>
#include <vector>

class empty_stack : public std::exception {
    std::string msg_;

   public:
    empty_stack(std::string msg) : msg_(msg) {}
    const char* what() const noexcept override { return msg_.c_str(); }
};
template <typename T>
class thread_safe_stack {
   private:
    std::stack<T> m_stack;
    std::mutex m_mutex;

   public:
    thread_safe_stack() : m_stack{}, m_mutex{} {}
    std::shared_ptr<T> pop() {
        std::lock_guard<std::mutex> g(m_mutex);
        if (m_stack.empty()) {
            throw empty_stack("empty stack");
        }
        std::shared_ptr<T> item = std::make_shared<T>(m_stack.top());
        m_stack.pop();
        return item;
    }
    void pop(T& ret) {
        std::lock_guard<std::mutex> g(m_mutex);
        if (m_stack.empty()) {
            throw empty_stack("empty stack");
        }
        ret = m_stack.top();
        m_stack.pop();
    }
    bool empty() {
        std::lock_guard<std::mutex> g(m_mutex);
        return m_stack.empty();
    }
    void push(T& item) {
        std::lock_guard<std::mutex> g(m_mutex);
        m_stack.push(item);
    }
};

int main() {
    try {
        std::vector<std::thread> producer(4);
        std::vector<std::thread> consumer(4);
        thread_safe_stack<int> stack;
        for (auto& pro : producer) {
            pro = std::thread(
                [](thread_safe_stack<int>& stack) {
                    for (int i = 0; i < 100; ++i) {
                        int data = 1;
                        stack.push(data);
                    }
                },
                std::ref(stack));
        }
        for (auto& consum : consumer) {
            consum = std::thread(
                [](thread_safe_stack<int>& stack) {
                    for (int i = 0; i < 100; ++i) {
                        try {
                            auto ret = stack.pop();
                            std::cout << *ret.get() << "\n";
                        } catch (std::exception e) {
                            std::cout << e.what();
                        }
                    }
                },
                std::ref(stack));
        }
        for (auto& pro : producer) {
            if (pro.joinable()) pro.join();
        }
        for (auto& con : consumer) {
            if (con.joinable()) con.join();
        }
        std::cout << (stack.empty() ? "OK\n" : "NOK\n");
    } catch (std::exception e) {
        std::cout << e.what();
    }

    return 0;
}
