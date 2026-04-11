// ## Bài 5 🟡 — `threadsafe_queue<T>`: Queue với condition variable
//
// ### Đặc tả hành vi
//
// Viết class template `threadsafe_queue<T>` với hai nhóm operation:
//
// **Non-blocking:**
// ```
// try_push(T value)
// try_pop(T& out) -> bool
// try_pop() -> shared_ptr<T>
// empty() -> bool
// ```
//
// **Blocking:**
// ```
// push(T value)
// wait_and_pop(T& out)
// wait_and_pop() -> shared_ptr<T>
// ```
//
// **Shutdown:**
// ```
// shutdown()
// ```
//
// `wait_and_pop` phải thực sự block — không busy-wait. Thread phải ngủ và được
// đánh thức khi có element mới.
//
// `shutdown()`: sau khi gọi, tất cả thread đang block ở `wait_and_pop` phải
// được unblock. Hành vi khi unblock do shutdown: ném exception `queue_shutdown`
// (tự định nghĩa) hoặc trả về trạng thái lỗi rõ ràng (tự chọn, nhưng phải nhất
// quán). Các lần `push` sau `shutdown` phải fail (ném exception hoặc return
// false).
//
// ### Ràng buộc cứng
//
// - Dùng `std::mutex` + `std::condition_variable`.
// - Dùng predicate form của `wait()` — không dùng `wait()` không có predicate.
// - Bên trong dùng `std::queue<T>`.
//
// ### Invariant cần giữ
//
// Mỗi element được push đúng một lần và được pop đúng một lần. Không có element
// bị mất, không có element bị pop hai lần.
//
// ### Bài test bắt buộc
//
// 3 producer threads, mỗi thread push 1000 số nguyên (thread 0: 0–999, thread
// 1: 1000–1999, thread 2: 2000–2999). 5 consumer threads, mỗi thread
// `wait_and_pop` trong loop. Main thread dùng `std::atomic<int>` đếm, khi đủ
// 3000 elements đã consumed thì gọi `shutdown()`. Sau khi join hết: tổng phải
// bằng 0+1+...+2999 = 4498500.
//
// ### Debug checkpoint
//
// - Nếu không có predicate trong `wait()` → spurious wakeup sẽ gây bug không
// deterministic. Tại sao?
// - Nếu `shutdown()` không gọi `notify_all()` → threads block mãi mãi.
// - Anti-pattern: dùng `empty()` rồi `wait_and_pop()` là TOCTOU race. Tại sao?
//
// ### Câu hỏi phân tích
//
// 1. Predicate trong `wait()` được evaluate ở đâu và khi nào? Mutex có bị hold
// trong lúc evaluate predicate không?
// 2. Nếu dùng `notify_one()` thay vì `notify_all()` trong `shutdown()` — điều
// gì xảy ra nếu có 5 threads đang block?
// 3. Tại sao `empty()` không đủ để làm predicate cho `wait()` trong context có
// `shutdown`?
//

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#define LOG                                                         \
    std::cout << "[threadID" << std::this_thread::get_id() << "] [" \
              << __FUNCTION__ << "]"

template <typename T>
class threadsafe_queue {
   public:
    class queue_exception : public std::runtime_error {
       public:
        queue_exception(std::string what_) : std::runtime_error(what_) {}
    };

    threadsafe_queue() : m_queue(), m_mutex(), m_cv(), m_isShutdown(false) {}
    bool try_push(T value) {
        std::unique_lock<std::mutex> ul_lock(m_mutex, std::try_to_lock);
        if (ul_lock.owns_lock()) {
            if (m_isShutdown) {
                return false;
            }
            m_queue.push(value);
            m_cv.notify_one();
            return true;
        }
        return false;
    }
    bool try_pop(T& out) {
        std::unique_lock<std::mutex> ul_lock(m_mutex, std::try_to_lock);
        if (ul_lock.owns_lock()) {
            if (m_queue.empty()) {
                throw queue_exception("empty queue");
            } else {
                out = m_queue.front();
                m_queue.pop();
                return true;
            }
        }
        return false;
    }
    std::shared_ptr<T> try_pop() {
        std::unique_lock<std::mutex> ul_lock(m_mutex, std::try_to_lock);
        if (ul_lock.owns_lock()) {
            if (m_queue.empty()) {
                throw queue_exception("empty queue");
            } else {
                try {
                    auto temp = std::make_shared<T>(m_queue.front());
                    m_queue.pop();
                    return temp;
                } catch (const std::exception& e) {
                    throw queue_exception("fail to pop: " +
                                          std::string(e.what()));
                }
            }
        }
        throw queue_exception("can't pop");
    }
    bool empty() const {
        std::lock_guard<std::mutex> g(m_mutex);
        return m_queue.empty();
    }
    void push(T value) {
        LOG << "\n";
        std::lock_guard<std::mutex> g(m_mutex);
        if (m_isShutdown) return;
        m_queue.push(value);
        m_cv.notify_one();
    }
    void wait_and_pop(T& out) {
        std::unique_lock<std::mutex> ul_lock(m_mutex);
        m_cv.wait(ul_lock, [&] { return !m_queue.empty() || m_isShutdown; });
        if (m_isShutdown) throw queue_exception("shutdown");
        out = m_queue.front();
        m_queue.pop();
    }
    std::shared_ptr<T> wait_and_pop() {
        LOG << "\n";
        std::unique_lock<std::mutex> ul_lock(m_mutex);
        m_cv.wait(ul_lock, [&] { return !m_queue.empty() || m_isShutdown; });
        if (m_isShutdown) throw queue_exception("shutdown");
        auto temp = std::make_shared<T>(m_queue.front());
        m_queue.pop();
        return temp;
    }
    void shutdown() {
        LOG << "\n";
        std::unique_lock<std::mutex> ul_lock(m_mutex);
        if (m_isShutdown) return;
        m_isShutdown = true;
        m_cv.notify_all();
    }

   private:
    std::queue<T> m_queue;
    mutable std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_isShutdown;
};

void test() {
    std::vector<std::thread> v_producers;
    v_producers.reserve(3);
    std::vector<std::thread> v_consumers;
    v_consumers.reserve(5);
    threadsafe_queue<int> queue;
    std::atomic<int> count = 0;
    for (int i = 0; i < 3; ++i) {
        v_producers.emplace_back(std::thread([&queue, i] {
            LOG << "\n";
            for (int k = i * 1000; k < i * 1000 + 1000; k++) {
                queue.push(k);
            }
        }));
    }
    int sum = 0;
    std::mutex mx_sum;
    for (int i = 0; i < 5; ++i) {
        v_consumers.emplace_back(std::thread([&] {
            while (1) {
                LOG << "\n";
                try {
                    int temp = *queue.try_pop().get();
                    std::lock_guard<std::mutex> g(mx_sum);
                    sum += temp;
                    count.fetch_add(1);
                } catch (const std::exception& e) {
                    LOG << e.what();
                    return;
                }
            }
        }));
    }
    for (auto& producer : v_producers) {
        std::cout << "join producer " << producer.get_id() << "\n";
        if (producer.joinable()) producer.join();
    }
    for (auto& consumer : v_consumers) {
        std::cout << "join consumer " << consumer.get_id() << "\n";
        if (consumer.joinable()) consumer.join();
    }
    queue.shutdown();
    std::cout << "count " << count << "\n";
    std::cout << "sum " << sum << "\n";
    int temp_sum = 0;
    for (int i = 0; i < 3000; ++i){
        temp_sum += i;
    }
    std::cout << "temp_sum " << temp_sum << "\n";
    std::cout << (sum == temp_sum ? "OK\n" : "NOK\n");
    return;
}
void test2() {
    std::vector<std::thread> v_producers;
    v_producers.reserve(3);
    std::vector<std::thread> v_consumers;
    v_consumers.reserve(5);
    threadsafe_queue<int> queue;
    std::atomic<int> count = 0;
    for (int i = 0; i < 3; ++i) {
        v_producers.emplace_back(std::thread([&queue, i] {
            LOG << "\n";
            for (int k = i * 1000; k < i * 1000 + 1000; k++) {
                queue.push(k);
            }
        }));
    }
    int sum = 0;
    std::mutex mx_sum;
    for (int i = 0; i < 5; ++i) {
        v_consumers.emplace_back(std::thread([&] {
            while (1) {
                LOG << count << "\n";
                try {
                    int temp = *queue.wait_and_pop().get();
                    std::lock_guard<std::mutex> g(mx_sum);
                    sum += temp;
                    count.fetch_add(1);
                } catch (const std::exception& e) {
                    LOG << e.what() << "\n";
                    return;
                }
            }
        }));
    }
    while (count < 3000) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    queue.shutdown();
    for (auto& producer : v_producers) {
        std::cout << "join producer " << producer.get_id() << "\n";
        if (producer.joinable()) producer.join();
    }
    for (auto& consumer : v_consumers) {
        std::cout << "join consumer " << consumer.get_id() << "\n";
        if (consumer.joinable()) consumer.join();
    }
    std::cout << "count " << count << "\n";
    std::cout << "sum " << sum << "\n";
    int temp_sum = 0;
    for (int i = 0; i < 3000; ++i){
        temp_sum += i;
    }
    std::cout << "temp_sum " << temp_sum << "\n";
    std::cout << (sum == temp_sum ? "OK\n" : "NOK\n");
    return;
}
int main() {
    try {
        test();
        test2();
    } catch (const std::exception& e) {
        LOG << e.what();
    }
    return 0;
}
