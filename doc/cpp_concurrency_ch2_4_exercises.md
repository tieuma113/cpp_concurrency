# Bài tập thực hành — C++ Concurrency in Action (Ch. 2–4)
> Tự thiết kế & implement từ đầu đến cuối.  
> Build: `g++ -std=c++17 -pthread -g -fsanitize=thread -fsanitize=address`

---

## Cách dùng bộ bài tập này

Mỗi bài có:
- **Đặc tả hành vi** — mô tả chính xác hệ thống phải làm gì
- **Ràng buộc cứng** — những gì bắt buộc hoặc cấm
- **Invariant cần giữ** — tính đúng đắn được định nghĩa thế nào
- **Debug checkpoint** — những điểm thường sai, để kiểm tra sau khi xong
- **Câu hỏi phân tích** — phải trả lời được sau khi implement xong

Không có skeleton code. Không có gợi ý cấu trúc class.
Tự thiết kế interface trước khi viết implementation.

---

# CHƯƠNG 2 — Managing Threads

---

## Bài 1 🟢 — `scoped_thread`: RAII wrapper cho `std::thread`

### Đặc tả hành vi

Viết một class `scoped_thread` thỏa mãn tất cả điều kiện sau:

1. Chỉ có thể được construct từ một `std::thread` đang joinable. Nếu thread không joinable tại thời điểm construct, constructor phải ném exception.

2. Khi `scoped_thread` bị hủy (destructor chạy), thread bên trong được join — **luôn luôn**, kể cả khi destructor được gọi do stack unwinding vì exception.

3. `scoped_thread` không thể copy (copy constructor và copy assignment đều bị xóa).

4. `scoped_thread` có thể move: move constructor và move assignment phải hoạt động đúng. Sau khi bị move đi, object nguồn phải ở trạng thái "empty" — destructor của nó không được gọi join trên thread đã chuyển đi.

5. Move assignment lên chính object đó (self-assignment) phải không làm hỏng gì.

### Ràng buộc cứng

- Không được dùng `std::jthread`.
- Chỉ dùng `<thread>`, `<stdexcept>`, `<utility>`.

### Invariant cần giữ

Ở mọi thời điểm, nếu `scoped_thread` đang giữ một thread, thì thread đó phải joinable. Trạng thái "giữ thread không joinable" không được tồn tại.

### Test cases bạn phải tự viết

Viết hàm `main()` kiểm tra toàn bộ 5 điều kiện trên. Mỗi điều kiện là một test case riêng, có `std::cout` rõ ràng. Không dùng framework nào.

### Debug checkpoint

- Chạy với `-fsanitize=thread`. Nếu có data race → sai.
- Nếu constructor ném exception sau khi `std::thread` đã được moved vào object, thread đó đi đâu?
- Move assignment nhận `std::thread` mới khi đang giữ thread cũ — thread cũ phải được xử lý thế nào?

### Câu hỏi phân tích

1. Tại sao check `joinable()` trong constructor chứ không trong destructor? Điều gì xảy ra nếu làm ngược lại?
2. Move assignment operator phải làm gì với thread **hiện tại đang giữ** trước khi nhận thread mới?
3. Phân biệt: `= default` vs viết tay move constructor — khi nào hai cái cho kết quả khác nhau ở bài này?

---

## Bài 2 🟡 — `parallel_accumulate`: Chia việc thủ công không dùng `std::async`

### Đặc tả hành vi

Viết hàm:

```
parallel_accumulate(Iterator first, Iterator last, T init) -> T
```

Hàm tính tổng của một dãy số bằng cách chia nhỏ dãy ra thành các đoạn, giao mỗi đoạn cho một thread riêng, rồi tổng hợp kết quả trên main thread.

Hành vi phải đúng với mọi kích thước input:
- Input rỗng → trả về `init`
- Input nhỏ hơn ngưỡng → chạy tuần tự, không spawn thread
- Input đủ lớn → spawn `std::thread::hardware_concurrency()` threads (tối thiểu 2)

Số lượng thread phải được tính toán **dựa trên kích thước input và hardware concurrency**, không hardcode.

### Ràng buộc cứng

- **Không dùng `std::async`, `std::future`, `std::promise`** trong bài này.
- Kết quả từ mỗi thread phải được trả về qua `std::vector` kết quả được cấp phát trước khi spawn threads.
- Không dùng bất kỳ mutex hay synchronization primitive nào — mỗi thread viết vào index riêng của nó.
- Main thread phải join tất cả workers trước khi tổng hợp kết quả.

### Invariant cần giữ

Kết quả trả về phải **giống hệt** `std::accumulate` chạy tuần tự trên cùng input (với T là integer). Viết test so sánh trực tiếp.

### Debug checkpoint

- Dùng `std::iota` để tạo vector 10 triệu phần tử. So sánh với `std::accumulate`.
- Thử với số phần tử không chia hết cho số thread. Kết quả vẫn phải đúng.
- Thử với `T = double`. Kết quả có thể khác do floating-point ordering — đây không phải bug, nhưng phải hiểu tại sao.

### Câu hỏi phân tích

1. Tại sao ràng buộc "mỗi thread viết vào index riêng" loại bỏ hoàn toàn nhu cầu mutex?
2. Nếu dùng `T = double` và kết quả khác `std::accumulate` — đây là bug hay behavior? Tại sao?
3. Vấn đề gì xảy ra nếu exception ném trong worker thread mà bạn không catch? Cơ chế gì của C++ runtime kích hoạt?

---

# CHƯƠNG 3 — Sharing Data Between Threads

---

## Bài 3 🟡 — `threadsafe_stack<T>`: Thread-safe stack không có race condition trong interface

### Đặc tả hành vi

Viết class template `threadsafe_stack<T>` với interface:

```
push(T value)
pop(T& out_value)              // overload 1: ghi vào biến
pop() -> shared_ptr<T>         // overload 2: trả về pointer
empty() -> bool
```

Yêu cầu về correctness:
- `push` và `pop` từ nhiều thread đồng thời không được tạo ra data race.
- Nếu stack rỗng và `pop` được gọi, phải ném exception `empty_stack` (tự định nghĩa).
- Copy constructor phải hoạt động đúng và thread-safe: trong lúc copy, stack nguồn phải bị lock.
- Copy assignment bị xóa.

### Ràng buộc cứng

- Bên trong dùng `std::stack<T>` hoặc `std::deque<T>` làm storage.
- Dùng đúng một `std::mutex`.
- Không được để bất kỳ method nào trả về reference hoặc raw pointer trỏ vào element bên trong (ngoại trừ `pop()` overload 2 trả về `shared_ptr`).
- `empty()` phải lock mutex — giải thích tại sao điều này cần thiết dù nó là `const`.

### Invariant cần giữ

Số lần `pop` thành công không bao giờ vượt quá số lần `push` trước đó. Không có element nào bị pop hai lần. Không có element nào bị mất.

### Bài test bắt buộc

4 producer threads đẩy 100 giá trị mỗi thread (tổng 400). 4 consumer threads pop liên tục cho đến khi tổng số element đã pop = 400. Dùng `std::atomic<int>` để đếm. Kết quả: tổng giá trị pop ra phải bằng tổng giá trị đã push vào.

### Debug checkpoint

- Chạy với `-fsanitize=thread`. Không được có report nào.
- Consumer phải loop với try/catch trên `empty_stack` — tại sao không dùng `empty()` trước khi `pop()`?
- Test: copy stack trong khi thread khác đang push. Kết quả có consistent không?

### Câu hỏi phân tích

1. Tại sao interface `top()` + `pop()` tách nhau lại inherently racy? Trace một interleaving cụ thể.
2. Tại sao `pop()` overload 2 trả về `shared_ptr<T>` thay vì `T` trực tiếp? Khi nào `unique_ptr` là lựa chọn tốt hơn?
3. `empty()` phải lock mutex — điều này không phá vỡ `const` semantics không? Giải thích vai trò của `mutable`.

---

## Bài 4 🔴 — `rw_cache<Key, Value>`: Read-write cache với `shared_mutex` và lazy init

### Đặc tả hành vi

Viết class template `rw_cache<Key, Value>` với interface:

```
find(Key) -> std::optional<Value>   // concurrent reads được phép đồng thời
update(Key, Value)                  // exclusive write
get_or_compute(Key, Fn) -> Value    // nếu có thì trả về; nếu chưa thì compute rồi cache
```

`get_or_compute(Key, Fn)` phải:
- Không giữ write lock trong khi `Fn()` đang chạy (Fn có thể tốn thời gian).
- Đảm bảo `Fn()` chỉ được gọi **đúng một lần** cho mỗi key, ngay cả khi nhiều thread cùng gọi với cùng key lúc cache chưa có.

### Ràng buộc cứng

- Dùng `std::shared_mutex` + `std::shared_lock` / `std::unique_lock`.
- `find()` phải dùng shared lock.
- `update()` phải dùng exclusive lock.
- **Không dùng `std::call_once`** trong bài này — implement double-checked pattern bằng tay.

### Invariant cần giữ

Với `get_or_compute`: kể cả khi 100 thread cùng gọi với cùng key lần đầu tiên, `Fn()` chỉ được gọi đúng 1 lần. Kiểm tra bằng `std::atomic<int>` đếm số lần `Fn` được gọi.

### Debug checkpoint

Đây là bài khó nhất Ch.3. Các lỗi phổ biến:
- Upgrade từ shared lock lên exclusive lock **không thể** làm trực tiếp — phải release shared lock trước. Không biết điều này sẽ deadlock.
- Sau khi release shared lock và acquire exclusive lock, phải **check lại** xem key đã được insert chưa. Không check → `Fn()` bị gọi nhiều lần.
- Đây là double-checked locking pattern — trace logic với 2 thread đồng thời cùng vào `get_or_compute` lần đầu.

### Câu hỏi phân tích

1. Tại sao không thể upgrade shared lock → exclusive lock một cách atomic trong C++? Điều gì xảy ra nếu hai thread cùng cố làm điều đó?
2. Nếu bỏ double-check sau khi acquire exclusive lock, invariant nào bị phá vỡ? Trace interleaving cụ thể.
3. So sánh solution của bạn với dùng `std::call_once` per key — trade-off là gì?

---

# CHƯƠNG 4 — Synchronizing Concurrent Operations

---

## Bài 5 🟡 — `threadsafe_queue<T>`: Queue với condition variable

### Đặc tả hành vi

Viết class template `threadsafe_queue<T>` với hai nhóm operation:

**Non-blocking:**
```
try_push(T value)
try_pop(T& out) -> bool
try_pop() -> shared_ptr<T>
empty() -> bool
```

**Blocking:**
```
push(T value)
wait_and_pop(T& out)
wait_and_pop() -> shared_ptr<T>
```

**Shutdown:**
```
shutdown()
```

`wait_and_pop` phải thực sự block — không busy-wait. Thread phải ngủ và được đánh thức khi có element mới.

`shutdown()`: sau khi gọi, tất cả thread đang block ở `wait_and_pop` phải được unblock. Hành vi khi unblock do shutdown: ném exception `queue_shutdown` (tự định nghĩa) hoặc trả về trạng thái lỗi rõ ràng (tự chọn, nhưng phải nhất quán). Các lần `push` sau `shutdown` phải fail (ném exception hoặc return false).

### Ràng buộc cứng

- Dùng `std::mutex` + `std::condition_variable`.
- Dùng predicate form của `wait()` — không dùng `wait()` không có predicate.
- Bên trong dùng `std::queue<T>`.

### Invariant cần giữ

Mỗi element được push đúng một lần và được pop đúng một lần. Không có element bị mất, không có element bị pop hai lần.

### Bài test bắt buộc

3 producer threads, mỗi thread push 1000 số nguyên (thread 0: 0–999, thread 1: 1000–1999, thread 2: 2000–2999). 5 consumer threads, mỗi thread `wait_and_pop` trong loop. Main thread dùng `std::atomic<int>` đếm, khi đủ 3000 elements đã consumed thì gọi `shutdown()`. Sau khi join hết: tổng phải bằng 0+1+...+2999 = 4498500.

### Debug checkpoint

- Nếu không có predicate trong `wait()` → spurious wakeup sẽ gây bug không deterministic. Tại sao?
- Nếu `shutdown()` không gọi `notify_all()` → threads block mãi mãi.
- Anti-pattern: dùng `empty()` rồi `wait_and_pop()` là TOCTOU race. Tại sao?

### Câu hỏi phân tích

1. Predicate trong `wait()` được evaluate ở đâu và khi nào? Mutex có bị hold trong lúc evaluate predicate không?
2. Nếu dùng `notify_one()` thay vì `notify_all()` trong `shutdown()` — điều gì xảy ra nếu có 5 threads đang block?
3. Tại sao `empty()` không đủ để làm predicate cho `wait()` trong context có `shutdown`?

---

## Bài 6 🟡 — `one_shot_pipeline`: Future/Promise chain không dùng `std::async`

### Đặc tả hành vi

Implement pipeline 3 stage bằng tay, dùng `std::promise` và `std::future`:

```
Stage 1 (Reader thread):   nhận raw string input → tokenize → trả ra string vector
Stage 2 (Parser thread):   nhận string vector → parse thành int vector
Stage 3 (Summer thread):   nhận int vector → tính tổng → trả ra int
```

Main thread:
1. Set up tất cả promise/future connections.
2. Launch 3 threads.
3. Truyền input vào Stage 1.
4. Chờ lấy kết quả cuối từ Stage 3.
5. Join tất cả threads.

Input ví dụ: `"10 20 30 40 50"` → `[10,20,30,40,50]` → `150`.

### Ràng buộc cứng

- **Không dùng `std::async`**.
- **Không dùng shared global variables** để truyền data giữa stages.
- Mỗi stage nhận input qua `future::get()` và truyền output qua `promise::set_value()`.
- Phải xử lý exception propagation: nếu Stage 2 ném exception (parse fail), Stage 3 phải nhận exception đó qua `future::get()`, và main thread phải thấy exception đó.

### Invariant cần giữ

Mỗi `std::promise` được `set_value()` hoặc `set_exception()` đúng một lần. Không bao giờ `get()` từ cùng một `future` hai lần.

### Bài test bắt buộc

Test 1 — happy path: `"10 20 30 40 50"`, kết quả = 150.
Test 2 — exception path: `"10 abc 30"`, Stage 2 ném `std::invalid_argument`, main thread catch được đúng exception type này.

### Debug checkpoint

- Nếu một stage ném exception nhưng quên gọi `set_exception()` trên promise → thread chờ downstream block mãi mãi.
- Destructor của `std::promise` khi chưa được set → tự động set `broken_promise`. Đây là safety net, không phải thiết kế đúng.
- Mỗi stage phải có try/catch để đảm bảo promise **luôn** được set trên mọi code path.

### Câu hỏi phân tích

1. Cơ chế nào cho phép exception "di chuyển" từ thread này sang thread khác qua future/promise?
2. `std::future` vs `std::shared_future` — nếu Stage 3's result cần được đọc bởi nhiều downstream consumers, cần thay đổi gì?
3. Tại sao destructor của `promise` chưa được set lại ném `broken_promise` thay vì `terminate()`?

---

## Bài 7 🔴 — `task_channel<Input, Output>`: Mini task pipeline (Synthesis)

### Đặc tả hành vi

Kết hợp `threadsafe_queue` (Bài 5) với `std::packaged_task` để build một pipeline nhận bất kỳ callable nào và execute async.

Viết class `task_channel<Input, Output>`:

```
// Constructor: N worker threads, processing function F
task_channel(size_t num_workers, std::function<Output(Input)> fn);

// Submit item để process, nhận future cho kết quả
std::future<Output> submit(Input item);

// Chờ hết queue rồi stop workers (idempotent)
void drain_and_stop();

// Destructor: gọi drain_and_stop nếu chưa gọi
~task_channel();
```

Workers lấy task từ internal queue, execute, result tự động set vào future thông qua `packaged_task`.

### Ràng buộc cứng

- Dùng `threadsafe_queue` từ Bài 5 làm internal queue.
- Dùng `std::packaged_task<Output(Input)>` để pair task với future.
- `drain_and_stop()` phải chờ tất cả submitted tasks hoàn thành trước khi join workers.
- Không dùng `std::async`.
- `drain_and_stop()` phải idempotent — gọi nhiều lần không crash.

### Invariant cần giữ

Với mỗi `submit(item)` thành công, future trả về sẽ eventually có giá trị — không block mãi mãi (trừ khi processing function bị block vô hạn).

### Bài test bắt buộc

Submit 100 tasks, mỗi task sleep random 1–10ms rồi trả về `input * input`. Collect tất cả futures. Sau `drain_and_stop()`, `get()` tất cả futures và verify kết quả đúng. Đo thời gian: phải nhanh hơn sequential rõ rệt với num_workers > 1.

### Debug checkpoint

- `std::packaged_task` không copyable — phải dùng `std::move` hoặc wrap trong `shared_ptr` khi đưa vào queue.
- Nếu `fn` ném exception, `packaged_task` tự động capture exception đó vào future. Verify bằng test riêng.
- Race condition: `submit()` sau khi `drain_and_stop()` đã được gọi — phải handle gracefully.

### Câu hỏi phân tích

1. Tại sao `std::packaged_task` không thể copy? Invariant nào của future/promise bị phá vỡ nếu nó được copy?
2. Trong destructor, nếu queue còn items chưa processed và workers đã bị stop, các futures tương ứng sẽ có trạng thái gì?
3. So sánh `task_channel` với `std::async(std::launch::async, ...)` về latency, throughput, overhead. Khi nào dùng cái nào?

---

# Thứ tự làm đề xuất

```
Bài 1 (Ch.2 RAII)
    ↓
Bài 2 (Ch.2 thread work division)
    ↓
Bài 3 (Ch.3 threadsafe_stack)
    ↓
Bài 5 (Ch.4 threadsafe_queue)    ← cần trước Bài 7
    ↓
Bài 4 (Ch.3 rw_cache)            ← độc lập, khó nhất Ch.3
    ↓
Bài 6 (Ch.4 future/promise chain)
    ↓
Bài 7 (Synthesis)
```

---

# Checklist "done" cho mỗi bài

- [ ] Compile sạch với `-Wall -Wextra`
- [ ] Chạy sạch với `-fsanitize=thread` (không có data race report)
- [ ] Chạy sạch với `-fsanitize=address` (không có memory error)
- [ ] Tất cả test cases pass, bao gồm exception paths
- [ ] Có thể trả lời tất cả câu hỏi phân tích bằng lời, không nhìn code
- [ ] Có thể trace worst-case interleaving mà thiết kế vẫn đúng
