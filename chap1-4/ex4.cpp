// ## Bài 4 🔴 — `rw_cache<Key, Value>`: Read-write cache với `shared_mutex` và
// lazy init
//
// ### Đặc tả hành vi
//
// Viết class template `rw_cache<Key, Value>` với interface:
//
// ```
// find(Key) -> std::optional<Value>   // concurrent reads được phép đồng thời
// update(Key, Value)                  // exclusive write
// get_or_compute(Key, Fn) -> Value    // nếu có thì trả về; nếu chưa thì
// compute rồi cache
// ```
//
// `get_or_compute(Key, Fn)` phải:
// - Không giữ write lock trong khi `Fn()` đang chạy (Fn có thể tốn thời gian).
// - Đảm bảo `Fn()` chỉ được gọi **đúng một lần** cho mỗi key, ngay cả khi nhiều
// thread cùng gọi với cùng key lúc cache chưa có.
//
// ### Ràng buộc cứng
//
// - Dùng `std::shared_mutex` + `std::shared_lock` / `std::unique_lock`.
// - `find()` phải dùng shared lock.
// - `update()` phải dùng exclusive lock.
// - **Không dùng `std::call_once`** trong bài này — implement double-checked
// pattern bằng tay.
//
// ### Invariant cần giữ
//
// Với `get_or_compute`: kể cả khi 100 thread cùng gọi với cùng key lần đầu
// tiên, `Fn()` chỉ được gọi đúng 1 lần. Kiểm tra bằng `std::atomic<int>` đếm số
// lần `Fn` được gọi.
//
// ### Debug checkpoint
//
// Đây là bài khó nhất Ch.3. Các lỗi phổ biến:
// - Upgrade từ shared lock lên exclusive lock **không thể** làm trực tiếp —
// phải release shared lock trước. Không biết điều này sẽ deadlock.
// - Sau khi release shared lock và acquire exclusive lock, phải **check lại**
// xem key đã được insert chưa. Không check → `Fn()` bị gọi nhiều lần.
// - Đây là double-checked locking pattern — trace logic với 2 thread đồng thời
// cùng vào `get_or_compute` lần đầu.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao không thể upgrade shared lock → exclusive lock một cách atomic
// trong C++? Điều gì xảy ra nếu hai thread cùng cố làm điều đó?
// 2. Nếu bỏ double-check sau khi acquire exclusive lock, invariant nào bị phá
// vỡ? Trace interleaving cụ thể.
// 3. So sánh solution của bạn với dùng `std::call_once` per key — trade-off là
// gì?
//
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <functional>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <shared_mutex>
#include <thread>
#include <unordered_map>
#include <vector>

struct computing_value {
    std::mutex m_mutex{};
    std::condition_variable cv{};
    bool finished = false;
};

template <typename Key, typename Value>
class rw_cache {
   private:
    std::unordered_map<Key, Value> m_cache;
    mutable std::shared_mutex m_mutex;
    mutable std::mutex m_computing_mutex;
    std::unordered_map<Key, std::shared_ptr<computing_value>> m_computing;

   public:
    rw_cache() : m_cache(), m_mutex(), m_computing() {}
    const std::optional<Value> find(const Key& k) {
        std::shared_lock<std::shared_mutex> g(m_mutex);
        if (m_cache.find(k) != m_cache.end()) {
            return m_cache.at(k);
        }
        return std::nullopt;
    }
    void update(const Key& k, const Value& v) {
        std::unique_lock<std::shared_mutex> g(m_mutex);
        if (m_cache.contains(k)) {
            m_cache.at(k) = v;
        }
    }
    Value get_or_compute(const Key& k, std::function<Value()> fn) {
        std::shared_lock<std::shared_mutex> sl_cache_lock(m_mutex);
        if (m_cache.contains(k)) {
            return m_cache.at(k);
        }
        sl_cache_lock.unlock();
        // if k is in the set -> it is computing -> wait until it finish
        // if k is not in the set -> it isn't compute or finished compute ->
        // check the
        std::unique_lock<std::mutex> ul_counting_lock(m_computing_mutex);
        if (m_computing.contains(k)){
            std::shared_ptr<computing_value> cp = m_computing.at(k);
            ul_counting_lock.unlock();
            std::unique_lock<std::mutex> ul_item_lock(cp->m_mutex);
            if (!cp->finished) cp->cv.wait(ul_item_lock, [&]{return cp->finished;});
        } else {
            auto cp = std::make_shared<computing_value>(); 
            m_computing.emplace(k, cp);
            ul_counting_lock.unlock();
            auto temp = fn();
            std::unique_lock<std::shared_mutex> ul_cache_lock(m_mutex);
            m_cache.emplace(k, temp);
            ul_cache_lock.unlock();
            std::unique_lock<std::mutex> ul_item_lock(cp->m_mutex);
            cp->finished = true;
            cp->cv.notify_all();
            ul_item_lock.unlock();
            ul_counting_lock.lock();
            m_computing.erase(k);
            ul_counting_lock.unlock();
        }
        sl_cache_lock.lock();
        return m_cache.at(k);
    }
};

void test1(){
    rw_cache<int, int> temp;
    auto t = temp.get_or_compute(1, []()->int{
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        std::cout << "do fn()\n";
        return 1;
    });
    temp.get_or_compute(1, []()->int{
        std::cout << "do fn()\n";
        return 1;
    });
    auto value = temp.find(1);
    if (value.has_value()) std::cout << value.value() << "\n";
    else std::cout << "no value for key 1\n";

    temp.update(1, 2);
    value = temp.find(1);
    if (value.has_value()) std::cout << value.value() << "\n";
    else std::cout << "no value for key 1\n";
}

void test2(){
    std::atomic<int> call_count{0};
    rw_cache<int, int> cache;

    std::vector<std::thread> threads;
    threads.reserve(100);
    for (int i = 0; i < 100; ++i) {
        threads.emplace_back(std::thread([&]{
            cache.get_or_compute(12, [&]{ 
                ++call_count;
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                return 99;
            });
        }));
    }
    for (auto& thread : threads){
        if (thread.joinable()) {
            thread.join();
        }
    }
    std::cout << (call_count == 1 ? "OK\n" : "NOK\n");
    std::cout << cache.find(12).value() << "\n";
}

int main() {
   test2(); 


    return 0;
}
