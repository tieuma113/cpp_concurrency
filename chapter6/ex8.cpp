// ## Bài 8 🟡 — `fine_queue<T>`: Queue với dummy node và tách head/tail mutex
//
// ### Đặc tả hành vi
//
// Viết class template `fine_queue<T>` dưới dạng singly linked list, với hai
// mutex tách biệt bảo vệ head và tail. Queue sử dụng kỹ thuật **dummy node** để
// tách biệt hoàn toàn vùng truy cập của push và pop.
//
// Interface:
//
// ```
// push(T value)
// try_pop() -> std::shared_ptr<T>
// try_pop(T& out) -> bool
// empty() -> bool
// ```
//
// Yêu cầu thiết kế:
// 1. Linked list nội bộ luôn có ít nhất một node (dummy node). Queue rỗng ⟺
// `head == tail`, cả hai trỏ vào dummy node.
// 2. `push()` chỉ lock `tail_mutex`. `try_pop()` lock `head_mutex`, và chỉ lock
// `tail_mutex` trong thời gian rất ngắn để đọc `tail`.
// 3. Data được lưu bằng `std::shared_ptr<T>` trong node. Dummy node có `data ==
// nullptr`.
// 4. `push()` phải thực hiện memory allocation (`make_shared`, `new node`)
// **trước** khi acquire lock.
// 5. Mỗi node sở hữu node tiếp theo qua `std::unique_ptr<node>`.
//
// ### Ràng buộc cứng
//
// - **Không dùng `std::queue`**, `std::deque`, hay bất kỳ container nào bên
// trong. Tự viết linked list.
// - **Không dùng condition variable** trong bài này — chỉ pure try-based
// interface.
// - Đúng hai mutex: `head_mutex` và `tail_mutex`. Không thêm mutex thứ ba.
// - `tail` là raw pointer (`node*`), `head` là `std::unique_ptr<node>`.
//
// ### Invariant cần giữ
//
// - `tail->next == nullptr` luôn đúng.
// - `tail->data == nullptr` luôn đúng (tail luôn là dummy).
// - `head == tail` ⟺ queue rỗng.
// - Với mỗi node `x` trong list mà `x != tail`: `x->data != nullptr` và
// `x->next` trỏ đến node tiếp theo.
// - Đi theo `next` từ `head` cuối cùng đến `tail`.
//
// ### Bài test bắt buộc
//
// 4 producer threads, mỗi thread push 50,000 giá trị (`int`). 4 consumer
// threads, mỗi thread `try_pop` trong loop cho đến khi tổng cộng đã pop 200,000
// elements (dùng `std::atomic<int>` đếm). Sau khi join: tổng giá trị pop ra
// phải bằng tổng giá trị đã push.
//
// ### Debug checkpoint
//
// - Chạy `-fsanitize=thread`. Nếu có report → sai. Đặc biệt chú ý data race
// trên `tail` — đọc `tail` trong `try_pop` **phải** lock `tail_mutex`.
// - Nếu `get_tail()` được gọi **ngoài** scope của `head_mutex` → có thể `head`
// bị di chuyển quá `tail` cũ. Trace interleaving cụ thể.
// - Khi queue có đúng 1 element: `head->next` là dummy, `head != tail`. Pop
// phải đưa `head` sang dummy. Sau pop: `head == tail`, queue rỗng.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao dummy node giải quyết được vấn đề `push` và `pop` cùng truy cập
// `head->next` / `tail->next` trên cùng một node? Trace trường hợp queue 1 phần
// tử **không có** dummy node.
// 2. Tại sao `get_tail()` phải được gọi **bên trong** scope của `head_mutex`?
// Nếu gọi ngoài, viết interleaving 2 thread gây sai.
// 3. `push()` thực hiện allocation ngoài lock — điều này cải thiện concurrency
// cụ thể như thế nào so với `std::queue`-based implementation?
// 4. Giải thích tại sao `tail` là raw pointer mà `head` là `unique_ptr`. Nếu
// đổi `tail` thành `unique_ptr` thì sao?
//
#include <atomic>
#include <chrono>
#include <exception>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

std::mutex log_mutex;

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
class fine_queue {
   private:
    struct my_node {
        std::shared_ptr<my_node> next;
        std::shared_ptr<my_node> prev;
        std::shared_ptr<T> value;
        my_node() : next(nullptr), prev(nullptr), value{} {}
        ~my_node() {
            LOG("value={} delete", value != nullptr ? *value.get() : -1);
        }
    };
    struct my_holder {
        alignas(std::hardware_destructive_interference_size);
        std::mutex m_mutex;
        alignas(std::hardware_destructive_interference_size);
        std::shared_ptr<my_node> m_sptr;
        my_holder() : m_mutex{}, m_sptr{} {}
    };
    my_holder m_hold_head;
    my_holder m_hold_tail;

   public:
    class my_exception : public std::runtime_error {
       public:
        my_exception(std::string what) : std::runtime_error(what.c_str()) {}
    };

    fine_queue() {
        m_hold_tail.m_sptr = std::make_shared<my_node>();
        m_hold_head.m_sptr = m_hold_tail.m_sptr;
    }

    ~fine_queue() { LOG("queue empty {}", empty() == true ? "true" : "false"); }
    void push(const T value) {
        LOG("push value {}", value);
        std::scoped_lock _lk(m_hold_tail.m_mutex);
        auto new_node = std::make_shared<T>(value);
        m_hold_tail.m_sptr->value = new_node;
        auto temp = std::make_shared<my_node>();
        m_hold_tail.m_sptr->next = temp;
        temp->prev = m_hold_tail.m_sptr;
        m_hold_tail.m_sptr = temp;
    }
    std::shared_ptr<T> try_pop() {
        std::scoped_lock<std::mutex> _lk_head(m_hold_head.m_mutex);
        {
            std::scoped_lock<std::mutex> _lk_tail(m_hold_tail.m_mutex);
            if (m_hold_head.m_sptr == m_hold_tail.m_sptr)
                throw my_exception("empty queue");
        }

        auto ret = m_hold_head.m_sptr->value;
        m_hold_head.m_sptr = m_hold_head.m_sptr->next;
        m_hold_head.m_sptr->prev = nullptr;
        LOG("Pop value {}", *ret.get());
        return ret;
    }
    // bool try_pop(T& out);
    bool empty() {
        if (m_hold_head.m_sptr == m_hold_tail.m_sptr) return true;
        return false;
    }
};

constexpr int INPUT = 50000;
constexpr int NUMBER_OF_PRODUCER = 4;
constexpr int NUMBER_OF_CONSUMER = 4;
void run_input(fine_queue<int>& test, std::atomic<int>& flag,
               std::atomic<int>& count) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    for (int i = 0; i < INPUT; ++i) {
        test.push(i);
        count.fetch_add(1);
    }
    flag.fetch_add(1);
    count.fetch_sub(1);
}
void run_output(fine_queue<int>& test, std::atomic<int>& flag,
                std::atomic<int>& count) {
    while (count.load() != -4 || flag.load() < 4) {
        LOG("count = {}", count.load());
        try {
            test.try_pop();
            count.fetch_sub(1);
        } catch (const std::exception& e) {
            LOG(e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }
}
int main() {
    fine_queue<int> test;
    std::atomic<int> count = 0;
    std::atomic<int> flag = 1;
    std::vector<std::jthread> v_producer_thread;
    v_producer_thread.reserve(4);
    std::vector<std::jthread> v_consumer_thread;
    v_consumer_thread.reserve(4);
    for (int i = 0; i < NUMBER_OF_PRODUCER; ++i) {
        v_producer_thread.push_back(std::jthread(
            run_output, std::ref(test), std::ref(flag), std::ref(count)));
    }
    for (int i = 0; i < NUMBER_OF_CONSUMER; ++i) {
        v_producer_thread.push_back(std::jthread(
            run_input, std::ref(test), std::ref(flag), std::ref(count)));
    }
    return 0;
}
