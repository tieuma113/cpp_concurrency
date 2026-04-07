// ## Bài 2 🟡 — `parallel_accumulate`: Chia việc thủ công không dùng
// `std::async`
//
// ### Đặc tả hành vi
//
// Viết hàm:
//
// ```
// parallel_accumulate(Iterator first, Iterator last, T init) -> T
// ```
//
// Hàm tính tổng của một dãy số bằng cách chia nhỏ dãy ra thành các đoạn, giao
// mỗi đoạn cho một thread riêng, rồi tổng hợp kết quả trên main thread.
//
// Hành vi phải đúng với mọi kích thước input:
// - Input rỗng → trả về `init`
// - Input nhỏ hơn ngưỡng → chạy tuần tự, không spawn thread
// - Input đủ lớn → spawn `std::thread::hardware_concurrency()` threads (tối
// thiểu 2)
//
// Số lượng thread phải được tính toán **dựa trên kích thước input và hardware
// concurrency**, không hardcode.
//
// ### Ràng buộc cứng
//
// - **Không dùng `std::async`, `std::future`, `std::promise`** trong bài này.
// - Kết quả từ mỗi thread phải được trả về qua `std::vector` kết quả được cấp
// phát trước khi spawn threads.
// - Không dùng bất kỳ mutex hay synchronization primitive nào — mỗi thread viết
// vào index riêng của nó.
// - Main thread phải join tất cả workers trước khi tổng hợp kết quả.
//
// ### Invariant cần giữ
//
// Kết quả trả về phải **giống hệt** `std::accumulate` chạy tuần tự trên cùng
// input (với T là integer). Viết test so sánh trực tiếp.
//
// ### Debug checkpoint
//
// - Dùng `std::iota` để tạo vector 10 triệu phần tử. So sánh với
// `std::accumulate`.
// - Thử với số phần tử không chia hết cho số thread. Kết quả vẫn phải đúng.
// - Thử với `T = double`. Kết quả có thể khác do floating-point ordering — đây
// không phải bug, nhưng phải hiểu tại sao.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao ràng buộc "mỗi thread viết vào index riêng" loại bỏ hoàn toàn nhu
// cầu mutex?
// 2. Nếu dùng `T = double` và kết quả khác `std::accumulate` — đây là bug hay
// behavior? Tại sao?
// 3. Vấn đề gì xảy ra nếu exception ném trong worker thread mà bạn không catch?
// Cơ chế gì của C++ runtime kích hoạt?

#include <bits/stdc++.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <iterator>
#include <random>
#include <thread>
#include <vector>

int number_of_thread(int length) {
    constexpr int MIN_ELEMENT = 25;
    if (length < MIN_ELEMENT) {
        return 0;
    }
    int hw_threads = std::thread::hardware_concurrency() == 0
                         ? 2
                         : std::thread::hardware_concurrency();
    int max_thread = (length + MIN_ELEMENT - 1) / MIN_ELEMENT;
    max_thread = std::min(hw_threads, max_thread);
    // std::cout << "Number of thread is " << max_thread << "\n";
    return max_thread;
}
template <typename Iter, typename T>
T m_accumulate(Iter begin, Iter end, T init) {
    T res = init;
    for (auto it = begin; it != end; it++) {
        res += *it;
    }
    // std::cout << "[threadID:" << std::this_thread::get_id()
    //           << "] res = " << init << "\n";
    return res;
}
template <typename Iter, typename T>
T parallel_accumulate(Iter begin, Iter end, T init) {
    auto run = [](Iter begin, Iter end, T& init) {
        for (auto it = begin; it != end; it++) {
            init += *it;
    }
        // std::cout << "[threadID:" << std::this_thread::get_id()
        //           << "] res = " << init << "\n";
    };
    auto length = std::distance(begin, end);
    int number_thread = number_of_thread(length);
    T res = init;
    std::vector<std::thread> thread_pool(number_thread);
    std::vector<T> res_value(number_thread, 0);
    auto element = length / number_thread;
    // Iter iter_temp = begin;
    for (int i = 0; i < number_thread; i++) {
        if (std::distance(begin, end) >= element) {
            auto next = std::next(begin, element);
            thread_pool[i] = std::thread(run, begin, next,
                                         std::ref(res_value.at(i)));
           begin = next; 
        } else {
            break;
        }
    }
    run(begin, end, res);
    for (auto& t : thread_pool) {
        if (t.joinable()) {
            t.join();
        }
    }
    for (auto const& i : res_value) {
        res += i;
    }
    return res;
}

int main() {
    std::cout << "main thread id: " << std::this_thread::get_id() << "\n";
    std::vector<int> test(1000000000);
    std::mt19937 gen(52);
    std::uniform_int_distribution<> dis(1, 100);
    std::generate(test.begin(), test.end(), [&] { return dis(gen); });
    auto t1 =std::chrono::steady_clock::now(); 
    std::cout << m_accumulate<decltype(test.begin()), long>(test.begin(),
                                                           test.end(), 2)
              << std::endl;
    auto t2 =std::chrono::steady_clock::now(); 
    std::cout << "single: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << "\n";
    t1 = std::chrono::steady_clock::now();
    std::cout << parallel_accumulate<decltype(test.begin()), long>(test.begin(),
                                                                  test.end(), 2)
              << std::endl;
    t2 = std::chrono::steady_clock::now();
    std::cout << "parallel: " << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << "\n";
    return 0;
}
