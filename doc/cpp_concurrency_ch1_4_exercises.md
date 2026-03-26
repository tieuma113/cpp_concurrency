# Bộ Bài Tập Code — C++ Concurrency in Action, Chương 1–4

> Mỗi bài có: **Skeleton với bug có chủ ý** → quan sát hành vi sai → sửa → trả lời câu hỏi phân tích.  
> Code: C++17. Build: `g++ -std=c++17 -pthread -g -fsanitize=thread`.  
> Độ khó: 🟢 Dễ · 🟡 Trung bình · 🔴 Khó

---

## Bài 1 — `scoped_thread`: RAII Wrapper cho `std::thread` 🟢

**Chương liên quan:** Ch2 (join/detach, move semantics, ownership)

**Bối cảnh:**  
`std::thread` không tự join khi destructor chạy — nếu thread còn joinable mà bị hủy,
chương trình gọi `std::terminate()`. Bài này yêu cầu viết `scoped_thread` đảm bảo
thread luôn được join khi ra khỏi scope, đồng thời không được copy.

### Skeleton (có bug)

```cpp
#include <thread>
#include <stdexcept>
#include <iostream>

class scoped_thread {
    std::thread t_;
public:
    explicit scoped_thread(std::thread t) : t_(std::move(t)) {
        if (!t_.joinable())
            throw std::logic_error("No thread");
    }

    // BUG 1: copy constructor và copy assignment không bị xóa
    scoped_thread(const scoped_thread&) = default;
    scoped_thread& operator=(const scoped_thread&) = default;

    ~scoped_thread() {
        // BUG 2: sai primitive — dùng detach thay vì join
        if (t_.joinable())
            t_.detach();
    }
};

void worker(int id) {
    std::cout << "Thread " << id << " running\n";
}

int main() {
    scoped_thread st(std::thread(worker, 42));
    std::cout << "Main doing work...\n";
    // st bị hủy ở đây — điều gì xảy ra?
}
```

### Câu hỏi phân tích

**Q1.** Bug 2 dùng `detach()` thay vì `join()`. Nếu `main()` kết thúc trước khi
thread `worker` in ra dòng chữ của nó, điều gì xảy ra với output? Tại sao?

**Q2.** Bug 1 không xóa copy constructor. Nếu ai đó viết:
```cpp
scoped_thread st2 = st;
```
Điều gì xảy ra ở compile time? Và nếu bằng cách nào đó nó compile được,
điều gì xảy ra ở runtime khi cả `st` và `st2` bị hủy?

**Q3.** Tại sao constructor kiểm tra `!t_.joinable()` và throw? Đưa ra một ví dụ
cụ thể về trường hợp người dùng vô tình truyền vào một `std::thread` không joinable.

**Q4. (L4 — Design)** `scoped_thread` có nên hỗ trợ move constructor không?
Nếu có: implement nó. Nếu không: giải thích tại sao di chuyển ownership lại
vô nghĩa hoặc nguy hiểm trong ngữ cảnh này.

### Yêu cầu sửa lỗi

1. Xóa copy constructor và copy assignment.
2. Sửa destructor để join thay vì detach.
3. (Bonus) Thêm move constructor và move assignment đúng chuẩn.

---

## Bài 2 — Thread-Safe Stack 🟡

**Chương liên quan:** Ch2 (launching threads) + Ch3 (mutex, interface design, exception safety)

**Bối cảnh:**  
`std::stack` không thread-safe. Bài này implement một thread-safe stack
theo cách cuốn sách chỉ ra, nhưng skeleton có bug trong interface design
tạo ra race condition ngay cả khi có lock.

### Skeleton (có bug)

```cpp
#include <stack>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <thread>
#include <iostream>
#include <vector>

struct empty_stack : std::exception {
    const char* what() const noexcept override { return "empty stack"; }
};

template<typename T>
class threadsafe_stack {
    std::stack<T> data_;
    mutable std::mutex m_;
public:
    threadsafe_stack() = default;

    void push(T value) {
        std::lock_guard<std::mutex> lock(m_);
        data_.push(std::move(value));
    }

    // BUG: interface split — top() và pop() là hai lời gọi riêng biệt
    // Đây là interface sai về mặt thread-safety
    T top() const {
        std::lock_guard<std::mutex> lock(m_);
        if (data_.empty()) throw empty_stack{};
        return data_.top();
    }

    void pop() {
        std::lock_guard<std::mutex> lock(m_);
        if (data_.empty()) throw empty_stack{};
        data_.pop();
    }

    bool empty() const {
        std::lock_guard<std::mutex> lock(m_);
        return data_.empty();
    }
};

// Hàm test: hai thread cùng pop từ một stack có 1 phần tử
void race_demo() {
    threadsafe_stack<int> s;
    s.push(42);

    auto pop_if_not_empty = [&]() {
        try {
            if (!s.empty()) {       // kiểm tra
                int v = s.top();    // đọc
                s.pop();            // xóa
                std::cout << "Popped: " << v << "\n";
            }
        } catch (const empty_stack&) {
            std::cout << "Stack was empty!\n";
        }
    };

    std::thread t1(pop_if_not_empty);
    std::thread t2(pop_if_not_empty);
    t1.join();
    t2.join();
}

int main() {
    race_demo();
}
```

### Câu hỏi phân tích

**Q1.** Vẽ worst-case interleaving giữa `t1` và `t2` trong `race_demo()` cho thấy
cả hai thread đều lọt qua `empty()` check nhưng chỉ một giá trị tồn tại.
Kết quả là gì?

**Q2.** Tại sao việc thêm lock bên trong `top()` và `pop()` riêng lẻ không đủ
để fix race condition này? Invariant nào đang bị vi phạm?

**Q3.** Sách đề xuất kết hợp `top()` và `pop()` thành một hàm duy nhất
`pop(std::shared_ptr<T>& value)` hoặc trả về `std::shared_ptr<T>`.
Tại sao dùng `shared_ptr` lại giải quyết được vấn đề exception safety
mà một interface `T pop()` thông thường không giải quyết được?

**Q4. (L4 — Trade-off)** Có thể thiết kế interface `bool try_pop(T& value)` thay thế không?
So sánh hai thiết kế (`shared_ptr` vs `try_pop`) về: exception safety, ease of use,
và performance. Khi nào bạn chọn cái nào?

### Yêu cầu sửa lỗi

Thay thế `top()` và `pop()` bằng interface đúng. Implement ít nhất **hai overload**:
```cpp
std::shared_ptr<T> pop();           // trả nullptr nếu rỗng
void pop(std::shared_ptr<T>& val);  // nhận giá trị qua out-param
```
Bảo đảm `pop()` là atomic: không có interleaving nào có thể đọc một giá trị
mà không xóa nó, hoặc xóa mà không đọc được.

---

## Bài 3 — Bounded Queue: Producer-Consumer 🔴

**Chương liên quan:** Ch3 (mutex) + Ch4 (condition_variable, predicate)

**Bối cảnh:**  
Implement một bounded queue thread-safe với giới hạn kích thước tối đa.
Producer block khi queue đầy; consumer block khi queue rỗng.
Skeleton có **3 bug** liên quan đến condition variable.

### Skeleton (có bug)

```cpp
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <iostream>
#include <chrono>
#include <stdexcept>

template<typename T>
class bounded_queue {
    std::queue<T> q_;
    std::mutex m_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_not_empty_;
    const size_t max_size_;
    bool shutdown_ = false;

public:
    explicit bounded_queue(size_t max_size) : max_size_(max_size) {}

    void push(T value) {
        std::unique_lock<std::mutex> lock(m_);

        // BUG 1: wait không có predicate — dễ bị spurious wakeup
        cv_not_full_.wait(lock);

        if (shutdown_) return;
        q_.push(std::move(value));

        // BUG 2: sai loại notify — notify_all thay vì notify_one
        cv_not_empty_.notify_all();
    }

    bool pop(T& value) {
        std::unique_lock<std::mutex> lock(m_);

        // BUG 3: predicate sai — thiếu điều kiện shutdown
        cv_not_empty_.wait(lock, [this] {
            return !q_.empty();
        });

        if (shutdown_ && q_.empty()) return false;
        value = std::move(q_.front());
        q_.pop();
        cv_not_full_.notify_one();
        return true;
    }

    void shutdown() {
        {
            std::lock_guard<std::mutex> lock(m_);
            shutdown_ = true;
        }
        cv_not_empty_.notify_all();
        cv_not_full_.notify_all();
    }
};

// --- Test: 2 producers, 3 consumers, queue size = 5 ---
int main() {
    bounded_queue<int> q(5);
    std::atomic<int> produced{0}, consumed{0};

    auto producer = [&](int id) {
        for (int i = 0; i < 10; ++i) {
            q.push(id * 100 + i);
            ++produced;
        }
    };

    auto consumer = [&]() {
        int val;
        while (q.pop(val)) {
            ++consumed;
        }
    };

    std::vector<std::thread> threads;
    threads.emplace_back(producer, 1);
    threads.emplace_back(producer, 2);
    for (int i = 0; i < 3; ++i)
        threads.emplace_back(consumer);

    // Cho producer chạy xong rồi shutdown
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    q.shutdown();

    for (auto& t : threads) t.join();

    std::cout << "Produced: " << produced << ", Consumed: " << consumed << "\n";
}
```

### Câu hỏi phân tích

**Q1.** Bug 1: `cv_not_full_.wait(lock)` không có predicate.
Mô tả **chính xác** spurious wakeup là gì, và điều gì xảy ra khi nó xảy ra
trong `push()` với code hiện tại — không chỉ "push vào queue đầy",
mà hãy trace qua state cụ thể.

**Q2.** Bug 2: `notify_all()` cho `cv_not_empty_`. Trong hệ thống có 3 consumers
đang block, khi 1 item được push vào, `notify_all()` làm gì?
Điều gì xảy ra với 2 consumer "thức dậy thừa"? Có gây incorrect behavior không,
hay chỉ là performance problem? Giải thích.

**Q3.** Bug 3: predicate của `pop()` là `[this] { return !q_.empty(); }`.
Construct tình huống cụ thể với shutdown sequence cho thấy
consumer có thể block vĩnh viễn (deadlock) do predicate này.

**Q4. (L5 — Design evaluation)** `shutdown()` gọi `notify_all()` cho cả hai CV.
Tại sao cần notify `cv_not_full_` khi shutdown? Và tại sao cần notify `cv_not_empty_`?
Điều gì xảy ra nếu quên notify một trong hai?

**Q5. (L5 — Stress test)** Giả sử thay `shutdown_` bằng `std::atomic<bool>` và bỏ lock
trong `shutdown()`. Race condition nào có thể xảy ra? Liệu `notify_all()` có đảm bảo
được gọi *sau* khi thread waiting đã vào trạng thái wait không?

### Yêu cầu sửa lỗi

Sửa cả 3 bug. Predicate đúng của `push()` và `pop()` phải đảm bảo:
- `push()`: chỉ proceed khi queue chưa đầy **hoặc** đang shutdown
- `pop()`: chỉ proceed khi queue không rỗng **hoặc** đang shutdown  
- Sau shutdown: tất cả blocked threads phải thoát ra được

---

## Bài 4 — Parallel Transform với `std::async` 🟡

**Chương liên quan:** Ch2 (hardware_concurrency) + Ch3 (safe accumulation) + Ch4 (std::async, std::future)

**Bối cảnh:**  
Implement `parallel_transform` — áp dụng một hàm lên mỗi phần tử của vector
song song bằng `std::async`, chia data thành N chunk tương ứng với số core.
Skeleton có bug về data race và future lifetime.

### Skeleton (có bug)

```cpp
#include <vector>
#include <future>
#include <functional>
#include <thread>
#include <iostream>
#include <numeric>
#include <algorithm>

// BUG 1: futures không được lưu lại — chúng bị hủy ngay lập tức
// std::async với launch::async có hành vi gì khi future bị hủy?
template<typename T, typename F>
std::vector<T> parallel_transform(std::vector<T> input, F func) {
    const size_t n = input.size();
    const size_t num_threads = std::thread::hardware_concurrency();
    const size_t chunk_size = (n + num_threads - 1) / num_threads;

    std::vector<T> result(n);

    for (size_t i = 0; i < num_threads; ++i) {
        size_t begin = i * chunk_size;
        size_t end = std::min(begin + chunk_size, n);
        if (begin >= n) break;

        // BUG 2: lambda capture result by reference — nhưng result
        // có thể bị move hoặc resize trong khi threads đang chạy không?
        // Và nếu hai chunks ghi vào các index khác nhau, có race không?
        std::async(std::launch::async, [&result, &input, &func, begin, end]() {
            for (size_t j = begin; j < end; ++j) {
                result[j] = func(input[j]);  // BUG 2 ở đây
            }
        });
        // Future bị drop ở đây (BUG 1)
    }

    return result;  // Trả về trước khi tất cả async tasks xong (do BUG 1)
}

int main() {
    std::vector<int> data(1'000'000);
    std::iota(data.begin(), data.end(), 0);

    auto result = parallel_transform(data, [](int x) { return x * x; });

    // Verify
    bool ok = true;
    for (size_t i = 0; i < result.size(); ++i) {
        if (result[i] != (int)(i * i)) { ok = false; break; }
    }
    std::cout << (ok ? "CORRECT" : "WRONG") << "\n";
}
```

### Câu hỏi phân tích

**Q1.** Bug 1: Future từ `std::async` bị drop ngay lập tức (không được gán vào biến).
Theo standard C++, destructor của `std::future` khi được tạo bởi `std::async` làm gì?
Điều này ảnh hưởng thế nào đến code trên — chương trình chạy sequential hay parallel?
Có đúng kết quả không?

**Q2.** Bug 2: Nhiều thread ghi vào các **index khác nhau** của cùng một `std::vector`.
Đây có phải data race không? Tham chiếu vào standard (hoặc reasoning từ memory model)
để justify câu trả lời của bạn — không chỉ nói "có" hoặc "không".

**Q3.** Nếu thay thế `parallel_transform` bằng cách dùng `std::packaged_task` thay vì
`std::async`, cấu trúc code thay đổi thế nào? Lợi ích/hạn chế so với `std::async`?

**Q4. (L4 — Edge case)** Nếu `n < num_threads` (ví dụ: 2 phần tử, 8 threads),
`chunk_size` tính ra bao nhiêu? Code có xử lý đúng trường hợp này không?
Trace qua loop để kiểm chứng.

**Q5. (L5 — Design)** `std::async(launch::async, ...)` không đảm bảo thread mới được
tạo trong mọi implementation — nó phụ thuộc vào thread pool của implementation.
Nếu cần **đảm bảo** đúng `N` threads chạy song song, bạn sẽ làm thế nào khác?
Trade-off là gì?

### Yêu cầu sửa lỗi

1. Lưu tất cả futures vào `std::vector<std::future<void>>`.
2. Sau loop, gọi `f.get()` cho từng future để sync.
3. Verify lại: Bug 2 có thực sự là race không sau khi fix Bug 1? Giải thích.
4. Bonus: Thêm exception propagation — nếu bất kỳ task nào throw, `parallel_transform`
   phải rethrow exception đó (gợi ý: `f.get()` đã làm điều này tự động).

---

## Bài 5 — Two-Stage Pipeline với `promise`/`future` 🔴

**Chương liên quan:** Ch4 (promise, future, shared_future, exception propagation)

**Bối cảnh:**  
Implement pipeline 2 stage:  
`[Loader thread]` → đọc data → `[Processor thread]` → tính toán → kết quả  
Sử dụng `std::promise`/`std::future` để truyền data giữa các stage.
Skeleton có bug về exception propagation và promise lifecycle.

### Skeleton (có bug)

```cpp
#include <future>
#include <thread>
#include <string>
#include <stdexcept>
#include <iostream>
#include <vector>
#include <numeric>

// Simulate loading data (có thể fail)
std::vector<int> load_data(bool should_fail) {
    if (should_fail)
        throw std::runtime_error("I/O error: disk read failed");
    return {1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
}

// Simulate processing
double process_data(const std::vector<int>& data) {
    double sum = std::accumulate(data.begin(), data.end(), 0.0);
    return sum / data.size();
}

void run_pipeline(bool simulate_failure) {
    std::promise<std::vector<int>> data_promise;
    std::future<std::vector<int>> data_future = data_promise.get_future();

    // Stage 1: Loader
    std::thread loader([&data_promise, simulate_failure]() {
        try {
            auto data = load_data(simulate_failure);
            data_promise.set_value(std::move(data));

            // BUG 1: set_value được gọi lần thứ hai sau khi đã set
            // (tưởng tượng đây là code cleanup nhầm lẫn)
            data_promise.set_value({});  // <- BUG
        } catch (const std::exception& e) {
            // BUG 2: exception bị nuốt — không được forward vào promise
            std::cerr << "Loader error: " << e.what() << "\n";
            // data_promise.set_exception(std::current_exception()); // bị comment out
        }
    });

    // Stage 2: Processor
    std::thread processor([&data_future]() {
        try {
            auto data = data_future.get();
            double result = process_data(data);
            std::cout << "Result: " << result << "\n";
        } catch (const std::exception& e) {
            std::cout << "Processor caught: " << e.what() << "\n";
        }
    });

    loader.join();
    processor.join();
}

int main() {
    std::cout << "=== Normal run ===\n";
    run_pipeline(false);

    std::cout << "\n=== Failure run ===\n";
    run_pipeline(true);
}
```

### Câu hỏi phân tích

**Q1.** Bug 1: `set_value()` được gọi hai lần trên cùng một promise.
Điều gì xảy ra? Trace qua hành vi của promise state machine:
promise ở trạng thái nào sau lần gọi đầu, và gọi lần hai có tác động gì?

**Q2.** Bug 2: Exception từ `load_data()` bị catch và in ra stderr,
nhưng không được forward vào promise. Processor thread đang block trên
`data_future.get()`. Điều gì xảy ra với processor thread sau khi loader thread kết thúc
mà không set value và không set exception?

**Q3.** Nếu sửa Bug 2 bằng cách uncomment `set_exception(std::current_exception())`,
và processor thread gọi `data_future.get()` — exception được throw ở đâu?
Trong loader thread hay trong processor thread? Giải thích cơ chế.

**Q4. (L4 — Design)** Giả sử muốn có **nhiều** processor threads cùng đọc kết quả
từ loader. `std::future` không thể share — bạn sẽ dùng cơ chế nào?
Viết signature của cách chuyển đổi từ `future` sang cơ chế đó.

**Q5. (L5 — Adversarial)** Destructor của `std::promise` khi promise chưa được set
(và future vẫn còn tồn tại) có hành vi gì? Construct một scenario trong pipeline này
nơi promise bị hủy sớm (ví dụ: loader thread crash trước khi set) — điều gì xảy ra
với processor thread đang block trên `get()`?

### Yêu cầu sửa lỗi

1. Xóa lần gọi `set_value()` thứ hai.
2. Sửa catch block để forward exception vào promise.
3. Đảm bảo dù `load_data()` thành công hay fail, promise luôn được set đúng một lần.
4. Bonus: Refactor loader dùng pattern RAII để đảm bảo promise luôn được set,
   kể cả khi có nhiều exit path (gợi ý: `std::shared_ptr` + custom deleter, hoặc scope guard).

---

## Bài tập tổng hợp — Không có Skeleton 🔴

### Bài T1: Thread-Safe Lazy Singleton

Implement một thread-safe singleton sử dụng `std::call_once` và `std::once_flag`.
- Instance chỉ được tạo một lần, kể cả khi 100 threads đồng thời gọi `get_instance()`
- Nếu constructor của singleton throw, lần gọi tiếp theo phải thử lại
- Không được dùng `static` local variable trick (phải dùng `call_once` explicitly)

### Bài T2: Asynchronous Result Cache

Implement `async_cache<K, V>` — cache kết quả của một hàm tính toán tốn kém:
- Lần đầu gọi `get(key)`: launch async task để tính giá trị, return future
- Các lần gọi tiếp theo với cùng key: return future đã có (không tính lại)
- Thread-safe: nhiều threads có thể gọi `get()` đồng thời
- Dùng: `std::mutex` + `std::unordered_map<K, std::shared_future<V>>`

### Bài T3: Fan-out/Fan-in với `shared_future`

Implement pattern:
- 1 thread "generator" tạo ra một giá trị (ví dụ: config, seed data)
- N worker threads đều cần đợi giá trị đó trước khi bắt đầu
- Sau khi tất cả workers xong, collect kết quả

Yêu cầu: dùng `std::shared_future` cho broadcast, `std::vector<std::future<T>>`
cho collect.

---

## Checklist tự đánh giá

Sau mỗi bài, tự hỏi:

| Câu hỏi | Trả lời được? |
|---------|--------------|
| Invariant của cấu trúc dữ liệu là gì? | |
| Worst-case interleaving trông như thế nào? | |
| Primitive nào protect gì, wait on gì? | |
| Nếu exception xảy ra giữa lock và unlock, điều gì xảy ra? | |
| Scale thế nào nếu tăng N threads? | |
| Có liveness issue (deadlock/livelock/starvation) không? | |
