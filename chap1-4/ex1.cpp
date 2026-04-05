// ## Bài 1 🟢 — `scoped_thread`: RAII wrapper cho `std::thread`
//
// ### Đặc tả hành vi
//
// Viết một class `scoped_thread` thỏa mãn tất cả điều kiện sau:
//
// 1. Chỉ có thể được construct từ một `std::thread` đang joinable. 
// Nếu thread không joinable tại thời điểm construct, constructor phải ném exception.
//
// 2. Khi `scoped_thread` bị hủy (destructor chạy), thread bên trong được join — **luôn luôn**, 
// kể cả khi destructor được gọi do stack unwinding vì exception.
//
// 3. `scoped_thread` không thể copy (copy constructor và copy assignment đều bị xóa).
//
// 4. `scoped_thread` có thể move: move constructor và move assignment phải hoạt động đúng. 
// Sau khi bị move đi, object nguồn phải ở trạng thái "empty" — destructor của nó không được gọi join trên thread đã chuyển đi.
//
// 5. Move assignment lên chính object đó (self-assignment) phải không làm hỏng gì.
//
// ### Ràng buộc cứng
//
// - Không được dùng `std::jthread`.
// - Chỉ dùng `<thread>`, `<stdexcept>`, `<utility>`.
//
// ### Invariant cần giữ
//
// Ở mọi thời điểm, nếu `scoped_thread` đang giữ một thread, thì thread đó phải joinable. 
// Trạng thái "giữ thread không joinable" không được tồn tại.
//
// ### Test cases bạn phải tự viết
//
// Viết hàm `main()` kiểm tra toàn bộ 5 điều kiện trên. Mỗi điều kiện là một test case riêng, có `std::cout` rõ ràng. 
// Không dùng framework nào.
//
// ### Debug checkpoint
//
// - Chạy với `-fsanitize=thread`. Nếu có data race → sai.
// - Nếu constructor ném exception sau khi `std::thread` đã được moved vào object, thread đó đi đâu?
// - Move assignment nhận `std::thread` mới khi đang giữ thread cũ — thread cũ phải được xử lý thế nào?
//
// ### Câu hỏi phân tích
//
// 1. Tại sao check `joinable()` trong constructor chứ không trong destructor? Điều gì xảy ra nếu làm ngược lại?
// 2. Move assignment operator phải làm gì với thread **hiện tại đang giữ** trước khi nhận thread mới?
// 3. Phân biệt: `= default` vs viết tay move constructor — khi nào hai cái cho kết quả khác nhau ở bài này?
//
#include <chrono>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <stdexcept>
#include <utility>
#include <iostream>

class scoped_thread {
    private:
        std::thread m_thread;
    public:
        scoped_thread(std::thread &t_) {
            if (t_.joinable()) {
                m_thread = std::move(t_);
            } else {
                throw std::runtime_error("the thread is not joinable"); 
            }
        }        
        ~scoped_thread() {
            if(m_thread.joinable()) {
                m_thread.join();
            }
        }
        scoped_thread(const scoped_thread& other) = delete;
        scoped_thread& operator=(const scoped_thread& other) = delete;
        scoped_thread(scoped_thread&& other){
            if (&other == this) {
                return; 
            }
            this->m_thread = std::move(other.m_thread);
            
        } 
        scoped_thread& operator=(scoped_thread&& other) {
            if (&other == this) {
                return other;
            }
            this->m_thread = std::move(other.m_thread);
            return *this;
        } 
};
constexpr int COUNT_MAX = 1000000;
void run(int &count, std::mutex& lock){
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < COUNT_MAX; i++) {
        std::lock_guard<std::mutex> g(lock);
        count++;
    }
}

int main() {
    std::function<void(int&)> test1 = [](int &count) {
        std::mutex lock;
        std::thread t1(run, std::ref(count), std::ref(lock));
        scoped_thread g1(t1);
        std::thread t2(run, std::ref(count), std::ref(lock));
        scoped_thread g2(t2);
    };
    try {
        int count = 0;
        test1(count);
        if (count == COUNT_MAX*2) std::cout << "OK\n";
        else std::cout << "FAIL\n";
    } catch (std::exception e) {
        std::cout << e.what() << '\n'; 
    }
}
