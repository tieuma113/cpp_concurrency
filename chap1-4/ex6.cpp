// ## Bài 6 🟡 — `one_shot_pipeline`: Future/Promise chain không dùng
// `std::async`
//
// ### Đặc tả hành vi
//
// Implement pipeline 3 stage bằng tay, dùng `std::promise` và `std::future`:
//
// ```
// Stage 1 (Reader thread):   nhận raw string input → tokenize → trả ra string
// vector Stage 2 (Parser thread):   nhận string vector → parse thành int vector
// Stage 3 (Summer thread):   nhận int vector → tính tổng → trả ra int
// ```
//
// Main thread:
// 1. Set up tất cả promise/future connections.
// 2. Launch 3 threads.
// 3. Truyền input vào Stage 1.
// 4. Chờ lấy kết quả cuối từ Stage 3.
// 5. Join tất cả threads.
//
// Input ví dụ: `"10 20 30 40 50"` → `[10,20,30,40,50]` → `150`.
//
// ### Ràng buộc cứng
//
// - **Không dùng `std::async`**.
// - **Không dùng shared global variables** để truyền data giữa stages.
// - Mỗi stage nhận input qua `future::get()` và truyền output qua
// `promise::set_value()`.
// - Phải xử lý exception propagation: nếu Stage 2 ném exception (parse fail),
// Stage 3 phải nhận exception đó qua `future::get()`, và main thread phải thấy
// exception đó.
//
// ### Invariant cần giữ
//
// Mỗi `std::promise` được `set_value()` hoặc `set_exception()` đúng một lần.
// Không bao giờ `get()` từ cùng một `future` hai lần.
//
// ### Bài test bắt buộc
//
// Test 1 — happy path: `"10 20 30 40 50"`, kết quả = 150.
// Test 2 — exception path: `"10 abc 30"`, Stage 2 ném `std::invalid_argument`,
// main thread catch được đúng exception type này.
//
// ### Debug checkpoint
//
// - Nếu một stage ném exception nhưng quên gọi `set_exception()` trên promise →
// thread chờ downstream block mãi mãi.
// - Destructor của `std::promise` khi chưa được set → tự động set
// `broken_promise`. Đây là safety net, không phải thiết kế đúng.
// - Mỗi stage phải có try/catch để đảm bảo promise **luôn** được set trên mọi
// code path.
//
// ### Câu hỏi phân tích
//
// 1. Cơ chế nào cho phép exception "di chuyển" từ thread này sang thread khác
// qua future/promise?
// 2. `std::future` vs `std::shared_future` — nếu Stage 3's result cần được đọc
// bởi nhiều downstream consumers, cần thay đổi gì?
// 3. Tại sao destructor của `promise` chưa được set lại ném `broken_promise`
// thay vì `terminate()`?
//
#include <exception>
#include <sys/types.h>

#include <future>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>
void run(std::string_view sv_input) {
    std::promise<std::vector<std::string>> prom_stage1;
    std::future<std::vector<std::string>> fu_stage1;
    fu_stage1 = prom_stage1.get_future();

    std::promise<std::vector<int>> prom_stage2;
    std::future<std::vector<int>> fu_stage2;
    fu_stage2 = prom_stage2.get_future();

    std::promise<int> prom_stage3;
    std::future<int> fu_stage3;
    fu_stage3 = prom_stage3.get_future();

    std::thread t_get_string_input([&]() {
        // do thing
        //  Stage 1 (Reader thread):   nhận raw string input → tokenize → trả ra
        //  string vector
        std::vector<std::string> vec_ret;
        ssize_t pos = sv_input.find(' ');
        ssize_t pre = -1;
        do {
            std::string_view sv_temp = sv_input.substr(pre+1, pos - pre); 
            std::cout << sv_temp << "\t";
            vec_ret.emplace_back(sv_temp);
            pre = pos;
            pos = sv_input.find(' ', pre+1);
        }while ( pre != std::string_view::npos);

        std::cout << "\n";
        prom_stage1.set_value(vec_ret);
        return;
    });

    std::thread t_parse_string_to_int([&] {
        auto input = fu_stage1.get();
        // do thing
        //  Stage 2 (Parser thread):   nhận string vector → parse thành int
        //  vector
        std::vector<int> vec_ret;
        for (const auto& item : input) {
            std::cout << item << "\t";
            try {
                vec_ret.emplace_back(std::stoi(item));
            } catch (const std::exception& e) {
                std::cout << "fail to convert " << item << " error: " << e.what();
            }
        }
        std::cout << "\n";
        prom_stage2.set_value(vec_ret);
        return;
    });

    std::thread t_sum_all_int([&] {
        auto input = fu_stage2.get();
        // do thing
        //  Stage 3 (Summer thread):   nhận int vector → tính tổng → trả ra int
        int i_ret = 0;
        for (const auto& item : input){
            std::cout << item << "\t";
            i_ret += item;
        }
        std::cout << "\n";
        prom_stage3.set_value(i_ret);
        return;
    });

    int ret = fu_stage3.get();
    t_get_string_input.join();
    t_parse_string_to_int.join();
    t_sum_all_int.join();
    std::cout << "value = " << ret << "\n";
}

int main() { 
    run("10 20 30 40 50");
    run("10 abc 30");
    return 0; }
