#include <algorithm>
#include <iostream>
#include <iterator>
#include <sstream>
#include <thread>
#include <utility>
#include <vector>
/*Bài 3 🟡 — Parallel Transform
Viết hàm:
cppvoid parallel_transform(std::vector<int>& data, int multiplier);
Chia data thành N đoạn bằng nhau, mỗi thread nhân tất cả phần tử trong đoạn của
nó với multiplier. N được tính từ hardware_concurrency(). Ràng buộc:

Không dùng global variable
Dùng std::ref đúng chỗ
Fallback về 2 nếu hardware_concurrency() trả về 0
*/
class thread_guard {
   private:
    std::thread m_thread;

   public:
    thread_guard(std::thread& _t) {
        if (_t.joinable()) {
            m_thread = std::move(_t);
        }
    }

    ~thread_guard() {
        if (m_thread.joinable()) {
            m_thread.join();
        }
    }

    thread_guard(const thread_guard&) = delete;
    thread_guard(thread_guard&&) = delete;
    thread_guard& operator=(const thread_guard&) = delete;
    thread_guard& operator=(thread_guard&&) = delete;
};

void parallel_transform(std::vector<int>& data, int multipiler) {
    int length = data.size();
    if (length == 0) {
        return;
    }

    unsigned long min_per_thread = 25;
    unsigned long max_thread = (length + min_per_thread - 1) / min_per_thread;
    unsigned long const hw_threads = std::thread::hardware_concurrency();
    unsigned long const number_of_threads =
        std::min(hw_threads != 0 ? hw_threads : 2, max_thread);
    unsigned long const block_size = length / number_of_threads;
    std::vector<std::thread> v_thread_list(number_of_threads);
    auto start = data.begin();
    for (int i = 0; i < number_of_threads; i++) {
        auto end = start;
        std::advance(end, block_size);
        v_thread_list[i] = std::thread(
            [](auto begin, auto end, int mul) {
                std::for_each(begin, end, [&](int& x) { x *= mul; });
            },
            start, end, multipiler);
        start = end;
    }
    for (auto& item : v_thread_list) {
        thread_guard g(item);
    }
}

int main() {
    std::vector<int> vec = {
        462, 117, 835, 290, 674, 58,  921, 343, 760, 105, 428, 551, 193,
        687, 724, 62,  849, 316, 537, 908, 215, 642, 479, 381, 702, 93,
        564, 127, 856, 398, 741, 250, 619, 85,  432, 573, 164, 901, 286,
        715, 47,  638, 512, 296, 779, 143, 860, 405, 671, 228, 559, 374,
        806, 91,  517, 263, 738, 154, 892, 420, 685, 309, 546, 72,  973,
        238, 611, 487, 354, 820, 167, 693, 41,  568, 249, 785, 103, 946,
        321, 657, 178, 504, 429, 763, 86,  615, 297, 832, 151, 574, 408,
        726, 63,  958, 274, 591, 385, 812, 147, 669};
    parallel_transform(vec, 2);
    std::stringstream ss;
    for (auto& item : vec) {
        ss << item << "\n";
    }
    std::cout << ss.str();
}
