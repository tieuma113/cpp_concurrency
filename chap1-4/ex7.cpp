// ## Bài 7 🔴 — `task_channel<Input, Output>`: Mini task pipeline (Synthesis)
//
// ### Đặc tả hành vi
//
// Kết hợp `threadsafe_queue` (Bài 5) với `std::packaged_task` để build một
// pipeline nhận bất kỳ callable nào và execute async.
//
// Viết class `task_channel<Input, Output>`:
//
// ```
// // Constructor: N worker threads, processing function F
// task_channel(size_t num_workers, std::function<Output(Input)> fn);
//
// // Submit item để process, nhận future cho kết quả
// std::future<Output> submit(Input item);
//
// // Chờ hết queue rồi stop workers (idempotent)
// void drain_and_stop();
//
// // Destructor: gọi drain_and_stop nếu chưa gọi
// ~task_channel();
// ```
//
// Workers lấy task từ internal queue, execute, result tự động set vào future
// thông qua `packaged_task`.
//
// ### Ràng buộc cứng
//
// - Dùng `threadsafe_queue` từ Bài 5 làm internal queue.
// - Dùng `std::packaged_task<Output(Input)>` để pair task với future.
// - `drain_and_stop()` phải chờ tất cả submitted tasks hoàn thành trước khi
// join workers.
// - Không dùng `std::async`.
// - `drain_and_stop()` phải idempotent — gọi nhiều lần không crash.
//
// ### Invariant cần giữ
//
// Với mỗi `submit(item)` thành công, future trả về sẽ eventually có giá trị —
// không block mãi mãi (trừ khi processing function bị block vô hạn).
//
// ### Bài test bắt buộc
//
// Submit 100 tasks, mỗi task sleep random 1–10ms rồi trả về `input * input`.
// Collect tất cả futures. Sau `drain_and_stop()`, `get()` tất cả futures và
// verify kết quả đúng. Đo thời gian: phải nhanh hơn sequential rõ rệt với
// num_workers > 1.
//
// ### Debug checkpoint
//
// - `std::packaged_task` không copyable — phải dùng `std::move` hoặc wrap trong
// `shared_ptr` khi đưa vào queue.
// - Nếu `fn` ném exception, `packaged_task` tự động capture exception đó vào
// future. Verify bằng test riêng.
// - Race condition: `submit()` sau khi `drain_and_stop()` đã được gọi — phải
// handle gracefully.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao `std::packaged_task` không thể copy? Invariant nào của
// future/promise bị phá vỡ nếu nó được copy?
// 2. Trong destructor, nếu queue còn items chưa processed và workers đã bị
// stop, các futures tương ứng sẽ có trạng thái gì?
// 3. So sánh `task_channel` với `std::async(std::launch::async, ...)` về
// latency, throughput, overhead. Khi nào dùng cái nào?
//
#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <mutex>
#include <queue>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

// Constructor: N worker threads, processing function F
// task_channel(size_t num_workers, std::function<Output(Input)> fn);

#define LOG(msg)                                                    \
    std::cout << "[ThreadID " << std::this_thread::get_id() << "] " \
              << __FUNCTION__ << ": " << msg << "\n";

template <typename T>
class thread_safe_queue {
   private:
    std::queue<T> m_queue;
    mutable std::mutex m_queue_mutex;
    mutable std::condition_variable m_queue_cv;

   public:
    class thread_safe_queue_exception : public std::runtime_error {
       public:
        thread_safe_queue_exception(const std::string& sv)
            : std::runtime_error(sv) {}
    };
    thread_safe_queue() : m_queue(), m_queue_cv(), m_queue_mutex() {}
    bool try_push(T const value) {
        std::unique_lock<std::mutex> ul_queue(m_queue_mutex, std::try_to_lock);
        if (ul_queue.owns_lock()) {
            m_queue.emplace(std::move(value));
            m_queue_cv.notify_one();
            return true;
        }
        return false;
    }

    std::shared_ptr<T> try_pop() {
        std::unique_lock<std::mutex> ul_queue(m_queue_mutex, std::try_to_lock);
        if (ul_queue.owns_lock()) {
            if (m_queue.empty()) {
                throw thread_safe_queue_exception("empty queue");
            }
            auto ret = std::make_shared<T>(std::move(m_queue.front()));
            m_queue.pop();
            return ret;
        }
        throw thread_safe_queue_exception("fail to lock");
    }

    bool empty() {
        std::lock_guard<std::mutex> g_queue(m_queue_mutex);
        return m_queue.empty();
    }

    void push(T value) {
        std::lock_guard<std::mutex> g(m_queue_mutex);
        m_queue.emplace(std::move(value));
        m_queue_cv.notify_one();
    }

    std::shared_ptr<T> wait_pop() {
        std::unique_lock<std::mutex> ul_queue(m_queue_mutex);
        auto status = m_queue_cv.wait_for(ul_queue, std::chrono::milliseconds(1000), [&] { return !m_queue.empty(); });
        if (!status) {
            throw thread_safe_queue_exception("time out");
        }
        auto ret = std::make_shared<T>(std::move(m_queue.front()));
        m_queue.pop();
        return ret;
    }
};

template <typename Output, typename Input>
class task_channel {
   private:
    thread_safe_queue<std::pair<std::packaged_task<Output(Input)>, Input>>
        m_queue;
    std::function<Output(Input)> m_exercute;
    std::vector<std::thread> m_workers;
    bool m_stop;
    std::mutex m_stop_mutex;

   public:
    task_channel(const int number_of_threads, std::function<Output(Input)> func)
        : m_queue(), m_exercute(func), m_stop(false), m_stop_mutex() {
        m_workers.reserve(number_of_threads);
        for (int i = 0; i < number_of_threads; ++i) {
            m_workers.emplace_back(std::thread([&] { worker(); }));
        }
    }
    std::future<Output> submit(Input item) {
        std::packaged_task<Output(Input)> task(m_exercute);
        std::future<Output> fu_result = task.get_future();
        m_queue.push({std::move(task), item});
        return fu_result;
    }
    void drain_and_stop() {
        LOG("");
        while (!m_queue.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            // wait
        }
        std::lock_guard<std::mutex> g(m_stop_mutex);
        m_stop = true;
    }

    ~task_channel() {
        drain_and_stop();
        for (auto& thread : m_workers) {
            if (thread.joinable()) thread.join();
        }
    }

   private:
    void worker() {
        LOG("");
        while (true) {
            {
                std::lock_guard<std::mutex> g(m_stop_mutex);
                if (m_stop) break;
            }
            try {
                auto handle_item = m_queue.wait_pop();
                std::packaged_task<Output(Input)> task =
                    std::move(handle_item.get()->first);
                auto& item = handle_item.get()->second;
                task(item);
            } catch (const std::exception& e) {
                LOG(e.what());
            }
        }
    }
};

int main() {
    task_channel<int, int> channel(5, [](int x) -> int { return x * x; });
    std::vector<std::future<int>> v_fu_ret(100000);
    for (int i = 0; i < 100000; ++i) {
        v_fu_ret[i] = channel.submit(i);
    }
    for (int i = 0; i < 100000; ++i) {
        int ret = v_fu_ret[i].get();
        std::stringstream ss;
        ss << i << " = " << ret;
        LOG(ss.str());
    }
    channel.drain_and_stop();
    return 0;
}
