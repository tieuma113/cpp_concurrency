// ## Bài 9 🔴 — `blocking_fine_queue<T>`: Thêm wait_and_pop + shutdown lên
// fine-grained queue
//
// ### Đặc tả hành vi
//
// Mở rộng `fine_queue` (Bài 8) thành `blocking_fine_queue<T>` với blocking
// interface:
//
// ```
// push(T value)
// try_pop() -> std::shared_ptr<T>
// try_pop(T& out) -> bool
// wait_and_pop() -> std::shared_ptr<T>
// wait_and_pop(T& out)
// empty() -> bool
// shutdown()
// ```
//
// `wait_and_pop()`:
// - Block nếu queue rỗng. Thread phải ngủ thật (condition variable), không
// busy-wait.
// - Predicate: `head.get() != get_tail()` (queue không rỗng) **hoặc** shutdown
// flag đã set.
// - Nếu được unblock do shutdown và queue vẫn rỗng: ném `queue_shutdown`
// exception.
//
// `shutdown()`:
// - Set flag, `notify_all()`.
// - Sau shutdown: `push()` ném `queue_shutdown`. `try_pop()` vẫn hoạt động bình
// thường cho đến khi queue cạn.
//
// **Exception safety trong wait_and_pop():**
// - Overload `wait_and_pop(T& out)` phải thực hiện `value =
// std::move(*head->data)` **trước** khi remove node khỏi list. Nếu copy/move
// throw, node vẫn còn trong queue.
//
// ### Ràng buộc cứng
//
// - Dùng `std::condition_variable` + `head_mutex` cho wait.
// - `notify_one()` trong `push()`, `notify_all()` trong `shutdown()`.
// - `notify_one()` phải được gọi **sau** khi đã release `tail_mutex` — giải
// thích tại sao.
// - Vẫn giữ hai mutex tách biệt như Bài 8k
//
// ### Invariant cần giữ
//
// Tất cả invariant của Bài 8, cộng thêm:
// - Sau `shutdown()`, mỗi thread đang block ở `wait_and_pop` phải eventually
// được unblock.
// - Không element nào bị mất: mọi element đã push trước shutdown đều pop được.
//
// ### Bài test bắt buộc
//
// **Test 1 — Normal flow:** 3 producers × 1000 items, 5 consumers dùng
// `wait_and_pop`. Khi đủ 3000 items consumed, main thread gọi `shutdown()`.
// Verify: tổng giá trị đúng, không thread nào bị block mãi.
//
// **Test 2 — Shutdown unblocks waiters:** Launch 5 consumer threads trước khi
// có bất kỳ producer nào. Tất cả 5 threads phải block. Sau 100ms, gọi
// `shutdown()`. Tất cả 5 threads phải catch `queue_shutdown` và kết thúc trong
// vòng 200ms.
//
// **Test 3 — Exception safety:** Push 10 items. Pop 5 bằng `try_pop`. Pop item
// 6 bằng `wait_and_pop(T&)` với `T` có copy/move constructor throw exception.
// Queue phải vẫn còn đúng 5 items.
//
// ### Debug checkpoint
//
// - `wait_for_data()` helper function phải return
// `std::unique_lock<std::mutex>` — tại sao không phải `lock_guard`?
// - Nếu `notify_one()` trong `push()` được gọi khi `tail_mutex` vẫn đang lock:
// thread consumer wakeup, cần lock `head_mutex`, rồi trong predicate gọi
// `get_tail()` cần lock `tail_mutex` → có bị block không?
// - Nếu dùng `notify_one()` trong `shutdown()` thay vì `notify_all()` — 4/5
// consumer threads sẽ block mãi.
//
// ### Câu hỏi phân tích
//
// 1. `wait_for_data()` trả `std::unique_lock` qua return — giải thích flow
// ownership của lock từ `wait_for_data()` → `wait_pop_head()` → caller.
// 2. Tại sao không dùng `data_cond.wait(lk, [&]{ return !empty(); })` làm
// predicate? (Hint: `empty()` acquire lock riêng.)
// 3. Khi exception xảy ra trong `wait_and_pop()` (ví dụ `make_shared` throw),
// thread đó không gọi `notify_one()` → thread khác đang chờ sẽ bỏ lỡ
// notification. Giải pháp là gì?
// 4. So sánh thiết kế này với `threadsafe_queue` single-mutex (Bài 5 cũ):
// concurrency đạt được ở những điểm cụ thể nào?
//

#include <condition_variable>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>

static std::mutex log_mutex;
template <typename... Args>
void log_impl(std::string func, int line, Args&&... args) {
    std::scoped_lock<std::mutex> _lk(log_mutex);
    std::cout << "[" << func << "]" << "[" << line << "]";
    ((std::cout << ' ' << std::forward<Args>(args)), ...);
    std::cout << '\n';
}

#define LOG(...)                                   \
    do {                                           \
        log_impl(__func__, __LINE__, __VA_ARGS__); \
    } while (0)

template <typename T>
class Fine_queue {
    // class queue_exception : public std::runtime_error{
    //     queue_exception(std::string& what): std::runtime_error(what.c_str())
    //     {}
    // };
    struct Node {
        std::shared_ptr<Node> next;
        std::unique_ptr<T> value;
        Node(T val) : next{nullptr}, value{std::make_unique<T>(val)} {};
    };
    struct Hold_item {
        alignas(std::hardware_destructive_interference_size);
        std::unique_ptr<std::mutex> mx;
        alignas(std::hardware_destructive_interference_size);
        std::shared_ptr<Node> node;
        Hold_item() : mx{}, node{nullptr} {}
    };
    Hold_item m_head;
    Hold_item m_tail;
    std::condition_variable m_cv;

   public:
    Fine_queue() : m_head{}, m_tail{}, m_cv{} {
        m_tail.node = std::make_shared<Node>(0);
        m_head.node = m_tail.node;
    }; 
    void push(const T& value) noexcept {
        std::scoped_lock<std::mutex> _lk(m_tail.mx);
        *m_tail.node->value.get() = value;
        m_tail.node->next = std::make_shared<Node>(0);
        m_tail.node = m_tail.node->next;
        m_cv.notify_one();
    }
    std::optional<std::shared_ptr<T>> try_pop() noexcept {
        std::scoped_lock<std::mutex> _lk_head(m_head.mx);
        {
            std::scoped_lock<std::mutex> _lk_tail(m_tail.mx);
            if (m_head.node == m_tail.node) return std::nullopt;
        }
        try {
            std::shared_ptr<T> ret = m_head.node->value;
            m_head.node = m_head.node->next;
            return ret;
        } catch (const std::exception& e) {
            LOG(e.what());
            return std::nullopt;
        }
    }
    std::optional<std::shared_ptr<T>> wait_and_pop() noexcept {
        std::unique_lock<std::mutex> _ulk_head(m_head.mx, std::defer_lock);
        {
            std::unique_lock<std::mutex> _ulk_tail(m_tail.mx, std::defer_lock);
            m_cv.wait(std::lock(_ulk_head, _ulk_tail),
                      [&]() -> bool { return m_head.node == m_tail.node; });
        }
       try {
            std::shared_ptr<T> ret = m_head.node->value;
            m_head.node = m_head.node->next;
            return ret;
        } catch (const std::exception& e) {
            LOG(e.what());
            return std::nullopt;
        } 
    }
    bool empty() {
        std::scoped_lock<std::mutex> _lk(m_head.mx, m_tail.mx);
        if (m_head.node == m_tail.node) return true;
        return false;
    }
    // shutdown()
};


int main() {
    Fine_queue<int> test_queue;
    
    return 0;
}
