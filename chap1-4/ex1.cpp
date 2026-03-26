// ## Bài 1 — `scoped_thread`: RAII Wrapper cho `std::thread` 🟢
//
// **Chương liên quan:** Ch2 (join/detach, move semantics, ownership)
//
// **Bối cảnh:**  
// `std::thread` không tự join khi destructor chạy — nếu thread còn joinable mà bị hủy,
// chương trình gọi `std::terminate()`. Bài này yêu cầu viết `scoped_thread` đảm bảo
// thread luôn được join khi ra khỏi scope, đồng thời không được copy.
//
// ### Skeleton (có bug)
//
// ### Câu hỏi phân tích
//
// **Q1.** Bug 2 dùng `detach()` thay vì `join()`. Nếu `main()` kết thúc trước khi
// thread `worker` in ra dòng chữ của nó, điều gì xảy ra với output? Tại sao?
// - Đó sẽ là unbehavior, output có thể hiện đúng hoặc mất output từ thread worker vì khi main hết thì cout có thể đẫ bị hủy -> thread in ra sẽ bị mất
// **Q2.** Bug 1 không xóa copy constructor. Nếu ai đó viết:
//     ```cpp
//     scoped_thread st2 = st;
// ```
// Điều gì xảy ra ở compile time? Và nếu bằng cách nào đó nó compile được,
// điều gì xảy ra ở runtime khi cả `st` và `st2` bị hủy?
//- Chương trình không compile được vì mặc định copy constructor bị delete. Nếu mà có thể build được thì sẽ có vấn đề lúc runtime vì std::thread là object move only
// **Q3.** Tại sao constructor kiểm tra `!t_.joinable()` và throw? Đưa ra một ví dụ
// cụ thể về trường hợp người dùng vô tình truyền vào một `std::thread` không joinable
//- Người dùng có thể truyền vào 1 std::thread đã bị move hoặc đã join hoặc detach -> những thread này đã giải phóng quyền sở hữu với pthread -> !joinable()
// **Q4. (L4 — Design)** `scoped_thread` có nên hỗ trợ move constructor không?
// Nếu có: implement nó. Nếu không: giải thích tại sao di chuyển ownership lại
// vô nghĩa hoặc nguy hiểm trong ngữ cảnh này.
//- Có ý nghĩa
// ### Yêu cầu sửa lỗi
//
// 1. Xóa copy constructor và copy assignment.
// 2. Sửa destructor để join thay vì detach.
// 3. (Bonus) Thêm move constructor và move assignment đúng chuẩn.
//
#include <chrono>
#include <thread>
#include <stdexcept>
#include <iostream>
#include <utility>

class scoped_thread {
    std::thread t_;
public:
    explicit scoped_thread(std::thread t) : t_(std::move(t)) {
        if (!t_.joinable())
            throw std::logic_error("No thread");
    }

    // BUG 1: copy constructor và copy assignment không bị xóa
    scoped_thread(const scoped_thread&) = delete;
    scoped_thread& operator=(const scoped_thread&) = delete;
    scoped_thread(scoped_thread&& other) : t_(std::move(other.t_)) {}
    scoped_thread& operator=(scoped_thread&& other) noexcept {
        if (this == &other) return *this;
        if(this->t_.joinable()){
            this->t_.join();
        }
        this->t_ = std::move(other.t_);
        return *this;
    }

    ~scoped_thread() {
        // BUG 2: sai primitive — dùng detach thay vì join
        if (t_.joinable())
            t_.join();
    }
};

void worker(int id) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    std::cout << "Thread " << id << " running\n";
}

int main() {
    scoped_thread st(std::thread(worker, 42));
    std::cout << "Main doing work...\n";
    return 0;
    // st bị hủy ở đây — điều gì xảy ra?
}


