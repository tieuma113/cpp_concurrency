// ## Bài 2 — Thread-Safe Stack 🟡
//
// **Chương liên quan:** Ch2 (launching threads) + Ch3 (mutex, interface design, exception safety)
//
// **Bối cảnh:**  
// `std::stack` không thread-safe. Bài này implement một thread-safe stack
// theo cách cuốn sách chỉ ra, nhưng skeleton có bug trong interface design
// tạo ra race condition ngay cả khi có lock.
//
// ### Skeleton (có bug)
//
// ```cpp
//
// ### Câu hỏi phân tích
//
// **Q1.** Vẽ worst-case interleaving giữa `t1` và `t2` trong `race_demo()` cho thấy
// cả hai thread đều lọt qua `empty()` check nhưng chỉ một giá trị tồn tại.
// Kết quả là gì?
//
// **Q2.** Tại sao việc thêm lock bên trong `top()` và `pop()` riêng lẻ không đủ
// để fix race condition này? Invariant nào đang bị vi phạm?
//
// **Q3.** Sách đề xuất kết hợp `top()` và `pop()` thành một hàm duy nhất
// `pop(std::shared_ptr<T>& value)` hoặc trả về `std::shared_ptr<T>`.
// Tại sao dùng `shared_ptr` lại giải quyết được vấn đề exception safety
// mà một interface `T pop()` thông thường không giải quyết được?
//
// **Q4. (L4 — Trade-off)** Có thể thiết kế interface `bool try_pop(T& value)` thay thế không?
// So sánh hai thiết kế (`shared_ptr` vs `try_pop`) về: exception safety, ease of use,
// và performance. Khi nào bạn chọn cái nào?
//
// ### Yêu cầu sửa lỗi
//
// Thay thế `top()` và `pop()` bằng interface đúng. Implement ít nhất **hai overload**:
// ```cpp
// std::shared_ptr<T> pop();           // trả nullptr nếu rỗng
// void pop(std::shared_ptr<T>& val);  // nhận giá trị qua out-param
// ```
// Bảo đảm `pop()` là atomic: không có interleaving nào có thể đọc một giá trị
// mà không xóa nó, hoặc xóa mà không đọc được.
//

#include <stack>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <thread>
#include <iostream>
#include <vector>

struct empty_stack : std::exception {
    const char* what() const noexcept override { return "empty stack"; }
};

template<typename T>
class threadsafe_stack {
    std::stack<T> data_;
    mutable std::mutex m_;
public:
    threadsafe_stack() = default;

    void push(T value) {
        std::lock_guard<std::mutex> lock(m_);
        data_.push(std::move(value));
    }

    // BUG: interface split — top() và pop() là hai lời gọi riêng biệt
    // Đây là interface sai về mặt thread-safety
    T top() const {
        std::lock_guard<std::mutex> lock(m_);
        if (data_.empty()) throw empty_stack{};
        return data_.top();
    }

    void pop() {
        std::lock_guard<std::mutex> lock(m_);
        if (data_.empty()) throw empty_stack{};
        data_.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_);
        return data_.empty();
    }
};

// Hàm test: hai thread cùng pop từ một stack có 1 phần tử
void race_demo() {
    threadsafe_stack<int> s;
    s.push(42);

    auto pop_if_not_empty = [&]() {
        try {
            if (!s.empty()) {       // kiểm tra
                int v = s.top();    // đọc
                s.pop();            // xóa
                std::cout << "Popped: " << v << "\n";
            }
        } catch (const empty_stack&) {
            std::cout << "Stack was empty!\n";
        }
    };

    std::thread t1(pop_if_not_empty);
    std::thread t2(pop_if_not_empty);
    t1.join();
    t2.join();
}

int main() {
    race_demo();
}

