# Bài tập code — C++ Concurrency in Action, Chapter 8: Designing Concurrent Code

Tài liệu nguồn: **Chapter 8 — Designing Concurrent Code** của *C++ Concurrency in Action* (Anthony Williams). Bao gồm: techniques for dividing work between threads (data division, recursive division, task type division, pipeline), performance factors (cache ping-pong, false sharing, data proximity, oversubscription), exception safety in parallel algorithms, scalability, và parallel implementations của `std::for_each`, `std::find`, `std::partial_sum`.

Toolchain: `g++ -std=c++17 -pthread -g -fsanitize=thread`

---

# PHẦN A — Exception Safety Infrastructure

---

## Bài 1 🟢 — `join_threads`: RAII guard cho vector of threads

### Đặc tả hành vi

Viết class `join_threads` — một RAII wrapper nhận reference tới `std::vector<std::thread>`, đảm bảo tất cả threads được join khi object bị hủy, bất kể scope exit bình thường hay do exception.

Interface:

```
class join_threads {
public:
    explicit join_threads(std::vector<std::thread>& threads);
    ~join_threads();

    join_threads(join_threads const&) = delete;
    join_threads& operator=(join_threads const&) = delete;
};
```

Hành vi destructor:
- Duyệt qua tất cả threads trong vector.
- Với mỗi thread `joinable()` → gọi `join()`.
- **Không** throw exception từ destructor.

### Ràng buộc cứng

- `join_threads` **không sở hữu** vector — chỉ giữ reference.
- Phải hoạt động đúng kể cả khi vector chưa được fill đầy (một số slot là default-constructed `std::thread`, tức `!joinable()`).
- Destructor phải safe khi thread đã được join trước đó (check `joinable()` trước mỗi `join()`).

### Invariant cần giữ

- Sau khi `join_threads` destructor chạy xong, **không thread nào** trong vector còn đang chạy.
- Nếu exception xảy ra sau khi `join_threads` được construct, destructor vẫn join tất cả — đây là toàn bộ lý do tồn tại của class.

### Bài test bắt buộc

**Test 1 — Normal exit:** Spawn 4 threads (mỗi thread sleep 50ms rồi increment atomic counter). Construct `join_threads` guard. Khi scope kết thúc bình thường: counter phải = 4.

**Test 2 — Exception exit:** Spawn 4 threads. Construct `join_threads` guard. Throw exception **trước** khi loop join thủ công. Catch exception bên ngoài scope. Counter vẫn phải = 4 (destructor đã join).

**Test 3 — Partial fill:** Vector size = 4, nhưng chỉ spawn 2 threads (slot 0, 1). Slot 2, 3 là default `std::thread`. Destructor phải skip slot 2, 3 mà không crash.

### Debug checkpoint

- Nếu destructor gọi `join()` trên thread **không** joinable → `std::system_error`. Phải check `joinable()` trước.
- Nếu constructor nhận `std::vector<std::thread>` by value thay vì by reference → threads bị move vào `join_threads`, vector gốc rỗng → caller không thể spawn thêm. **Phải là reference.**

### Câu hỏi phân tích

1. So sánh `join_threads` với approach dùng try/catch bọc toàn bộ spawn loop + join loop. Approach nào robust hơn khi có nhiều exception throw points?
2. `std::thread` destructor gọi `std::terminate` nếu thread joinable — `join_threads` ngăn chặn điều này bằng cách nào chính xác?
3. Nếu muốn support cả `join` lẫn `detach` (configurable), interface thay đổi thế nào?
4. Class này có nên là `moveable` không? Nếu cho phép move, ownership semantics thay đổi ra sao?

---

## Bài 2 🟢 — `parallel_accumulate`: Exception-safe với `packaged_task` + `future`

### Đặc tả hành vi

Viết function template `parallel_accumulate` — tính tổng các elements trong range `[first, last)` bằng nhiều threads, với đầy đủ exception safety.

Interface:

```
template <typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init);
```

Hành vi:
- Chia range thành `num_threads` chunks dựa trên `std::thread::hardware_concurrency()`.
- Minimum `25` elements per thread — nếu ít hơn thì giảm số threads.
- Mỗi worker thread tính partial sum cho chunk của mình qua `std::packaged_task`.
- Main thread tính chunk cuối cùng.
- Kết quả cuối cùng = `init` + tổng tất cả partial sums.

### Ràng buộc cứng

- Dùng `std::packaged_task<T(Iterator, Iterator)>` cho mỗi worker thread.
- Dùng `std::future<T>` để lấy kết quả và propagate exception.
- Dùng `join_threads` (Bài 1) để đảm bảo thread safety khi exception.
- **Không dùng `std::async`** — bài này tập trung vào manual thread management.
- Worker function (`accumulate_block`) phải **return** giá trị, không write vào shared reference.

### Invariant cần giữ

- Nếu bất kỳ worker thread nào throw exception, exception được propagate tới caller qua `future::get()`.
- Nếu exception xảy ra giữa chừng (ví dụ thread constructor throw), tất cả threads đã spawn được join trước khi exception propagate — nhờ `join_threads`.
- Kết quả chính xác: bằng `std::accumulate(first, last, init)` nếu không có exception.

### Bài test bắt buộc

**Test 1 — Correctness:** Vector 100,000 ints (1 đến 100,000). `parallel_accumulate` phải trả về đúng `5000050000LL`. So sánh với `std::accumulate`.

**Test 2 — Exception propagation:** Custom iterator type mà dereference throw `std::runtime_error` khi gặp giá trị đặc biệt (ví dụ giá trị = -1). Đặt -1 ở giữa range. `parallel_accumulate` phải throw exception, không terminate.

**Test 3 — Small range:** Vector 10 ints. Phải chạy single-threaded (dưới min_per_thread). Kết quả vẫn đúng.

**Test 4 — Empty range:** `parallel_accumulate(v.end(), v.end(), 42)` phải trả về `42`.

### Debug checkpoint

- `accumulate_block` operator() phải return `T`, không nhận `T& result` tham chiếu — vì `packaged_task` capture return value.
- `packaged_task` phải được **move** vào `std::thread` constructor — `packaged_task` không copyable.
- `futures[i].get()` sẽ block cho đến khi worker thread hoàn thành — không cần explicit join **trước** get(). Nhưng `join_threads` vẫn cần thiết để handle trường hợp exception xảy ra trước khi gọi get().
- Khi accumulate `futures[i].get()`, nếu future chứa exception → throw ngay tại đó. Các futures sau đó **không được get()** — nhưng `join_threads` destructor vẫn join tất cả threads.

### Câu hỏi phân tích

1. Tại sao `accumulate_block` phải return value thay vì write vào `T& result`? Nếu write vào reference, exception trên worker thread xử lý thế nào?
2. Nếu 2 worker threads cùng throw exception, exception nào được propagate tới caller? Cái còn lại đi đâu?
3. `join_threads` destructor chạy **trước** hay **sau** `futures` vector destructor? Thứ tự này quan trọng không? (Hint: declaration order.)
4. So sánh cách tiếp cận packaged_task với cách dùng raw threads + shared `std::vector<T> results` (version naive). Liệt kê mọi failure mode mà version naive có nhưng version này không.

---

## Bài 3 🟡 — `parallel_accumulate_async`: Recursive subdivision với `std::async`

### Đặc tả hành vi

Viết function template `parallel_accumulate` dùng `std::async` + recursive subdivision — thay thế hoàn toàn Bài 2 bằng approach đơn giản hơn.

Interface:

```
template <typename Iterator, typename T>
T parallel_accumulate(Iterator first, Iterator last, T init);
```

Hành vi:
- Nếu `length <= max_chunk_size` (= 25): gọi `std::accumulate` trực tiếp.
- Ngược lại: chia range làm đôi tại midpoint.
  - Nửa đầu: spawn qua `std::async` (recursive call).
  - Nửa sau: recursive call trực tiếp trên current thread.
  - Return `first_half.get() + second_half`.

### Ràng buộc cứng

- **Chỉ dùng `std::async`** — không tạo `std::thread` thủ công.
- Không cần `join_threads` — `std::future` destructor tự wait.
- Không cần `std::packaged_task` — `std::async` tự tạo.
- Recursive subdivision — không pre-calculate số threads.

### Invariant cần giữ

- Exception safety tự động: nếu recursive call throw, `std::future` destructor wait cho async task hoàn thành trước khi propagate.
- Nếu async task throw, `future::get()` rethrow.
- Library tự quyết định chạy asynchronously hay deferred — không oversubscription.

### Bài test bắt buộc

**Test 1 — Correctness:** Giống Bài 2, Test 1. Kết quả phải khớp `std::accumulate`.

**Test 2 — Exception propagation:** Giống Bài 2, Test 2. Exception phải propagate, không terminate.

**Test 3 — Large range:** Vector 10,000,000 ints. Đo thời gian so với `std::accumulate` sequential. Phải nhanh hơn trên máy multi-core (ít nhất 1.5x).

### Debug checkpoint

- `std::async` phải nhận function pointer dạng `&parallel_accumulate<Iterator, T>` — nếu dùng lambda capture `[&]` thì phải cẩn thận lifetime.
- Nếu gọi `first_half.get()` **sau** recursive call trực tiếp: đúng. Nếu gọi **trước**: sequential, mất parallelism.
- `std::async` có thể chạy deferred — nếu tất cả đều deferred, code chạy sequential. Kiểm tra bằng benchmark.

### Câu hỏi phân tích

1. So sánh code length và complexity giữa Bài 2 (manual threads) và Bài 3 (async). Liệt kê cụ thể những gì Bài 3 không cần viết.
2. `std::async` có thể launch deferred thay vì async — điều này ảnh hưởng đến parallelism thế nào? Khi nào library chọn deferred?
3. Recursive subdivision tạo binary tree of tasks — chiều sâu tối đa là bao nhiêu cho range N elements, chunk size 25? Mỗi node tạo 1 future — bao nhiêu futures tồn tại đồng thời tối đa?
4. Nếu thay `std::async(fn, ...)` bằng `std::async(std::launch::async, fn, ...)` — force async — điều gì xảy ra với range 10 triệu elements?

---

# PHẦN B — Parallel Algorithms

---

## Bài 4 🟡 — `parallel_for_each`: Hai versions — manual threads + `std::async`

### Đặc tả hành vi

Viết function template `parallel_for_each` — áp dụng function `f` lên mỗi element trong range `[first, last)` song song.

**Version 1 — Manual threads:**

```
template <typename Iterator, typename Func>
void parallel_for_each(Iterator first, Iterator last, Func f);
```

Chia range thành chunks, mỗi chunk chạy trên thread riêng qua `std::packaged_task<void(void)>`. Dùng `join_threads` + `future<void>::get()` cho exception safety.

**Version 2 — `std::async`:**

```
template <typename Iterator, typename Func>
void parallel_for_each_async(Iterator first, Iterator last, Func f);
```

Recursive subdivision, spawn nửa đầu qua `std::async`, xử lý nửa sau trực tiếp.

### Ràng buộc cứng

- Version 1: dùng `std::packaged_task<void(void)>` + lambda capture `[=]` (copy block_start, block_end, f). Dùng `join_threads`.
- Version 2: dùng `std::async`. Base case khi `length < 2 * min_per_thread`.
- `Func` nhận `typename Iterator::reference` (có thể modify element).
- Cả hai version phải propagate exception từ `f` tới caller.

### Invariant cần giữ

- Mọi element trong range được gọi `f` đúng **một lần**.
- Thứ tự gọi `f` **không** được đảm bảo (khác với `std::for_each` sequential).
- Nếu `f` throw trên bất kỳ element nào, exception propagate tới caller. Các threads khác vẫn được join sạch (version 1) hoặc future destructor wait (version 2).

### Bài test bắt buộc

**Test 1 — Mutation correctness:** Vector 10,000 ints. `parallel_for_each(v.begin(), v.end(), [](int& x) { x *= 2; })`. Verify mọi element đã nhân đôi.

**Test 2 — Exception propagation:** `f` throw khi gặp giá trị 42. `parallel_for_each` phải throw, không terminate. Dùng try/catch verify.

**Test 3 — Empty range:** Không crash, không gọi `f`.

**Test 4 — Benchmark vs sequential:** Vector 1,000,000 ints. `f` = heavy computation (ví dụ `x = static_cast<int>(std::sqrt(static_cast<double>(x * x + 1)))` lặp 100 lần). Parallel phải nhanh hơn sequential trên multi-core.

### Debug checkpoint

- Version 1: `futures[i].get()` với `future<void>` — **vẫn cần gọi** dù không cần giá trị. Nếu không gọi, exception trong worker bị nuốt.
- Version 1: lambda phải capture `block_start, block_end` **by value** — nếu capture by reference, biến local đã thay đổi khi thread chạy.
- Version 2: `first_half.get()` ở cuối — nếu async task throw, get() rethrow. Nếu recursive call throw, future destructor wait cho async task.

### Câu hỏi phân tích

1. `parallel_for_each` không cần reduction step (không có kết quả trả về). So với `parallel_accumulate`, exception safety đơn giản hơn hay phức tạp hơn? Tại sao?
2. Tại sao `packaged_task<void(void)>` chứ không phải `packaged_task<void(Iterator, Iterator)>`? (Hint: lambda đã capture iterators.)
3. Version async tự scale số threads — version manual threads dùng `hardware_concurrency()`. Trong trường hợp nào version manual tốt hơn?
4. Nếu `f` là stateful (ví dụ accumulate local counter), mỗi thread nhận copy riêng hay share? Hệ quả với kết quả?

---

## Bài 5 🔴 — `parallel_find`: Early termination với `std::promise` + `std::atomic<bool>`

### Đặc tả hành vi

Viết function template `parallel_find` — tìm element đầu tiên bằng `match` trong range, dừng sớm khi tìm thấy.

**Version 1 — Manual threads + promise:**

```
template <typename Iterator, typename MatchType>
Iterator parallel_find(Iterator first, Iterator last, MatchType match);
```

**Version 2 — `std::async` + recursive:**

```
template <typename Iterator, typename MatchType>
Iterator parallel_find_async(Iterator first, Iterator last, MatchType match);
```

### Đặc tả chi tiết Version 1

Chia range thành chunks. Mỗi worker thread:
- Duyệt chunk, check `done_flag` sau mỗi element.
- Nếu tìm thấy match: dùng `done_flag.exchange(true)` — chỉ thread nhận `false` cũ mới gọi `result.set_value(iterator)`.
- Nếu exception: catch, dùng `result.set_exception(std::current_exception())`, set `done_flag`.
- `set_exception` cũng có thể throw nếu promise đã set → catch và bỏ qua (nested try/catch).

Main function:
- Spawn threads trong block scope với `join_threads`.
- Sau join: check `done_flag`. Nếu false → return `last`. Nếu true → return `result.get_future().get()`.

### Đặc tả chi tiết Version 2

Delegate tới `parallel_find_impl` nhận thêm `std::atomic<bool>& done`:
- Base case: linear search, check `done` mỗi element.
- Recursive case: spawn nửa sau qua `std::async`, search nửa đầu trực tiếp.
- Nếu direct search thành công → return iterator, không cần check async result.
- Nếu direct search fail (return `mid_point`) → return `async_result.get()`.
- Wrap toàn bộ trong try/catch: set `done = true` nếu exception.

### Ràng buộc cứng

- Version 1: `std::promise<Iterator>` + `std::atomic<bool> done_flag`. Dùng `join_threads`.
- Version 2: `std::async` + `std::atomic<bool>& done` passed by `std::ref`.
- `done_flag.exchange(true)` — **không phải** `done_flag.store(true)` — để tránh set promise hai lần.
- Không dùng `std::find` bên trong — viết manual loop để check `done_flag` mỗi iteration.

### Invariant cần giữ

- Nếu match tồn tại trong range: return iterator tới **một** match (không nhất thiết đầu tiên theo thứ tự).
- Nếu match không tồn tại: return `last`.
- Nếu exception xảy ra trên bất kỳ thread nào: propagate tới caller (version 1 qua promise, version 2 qua future).
- Early termination: khi match được tìm thấy, các threads khác dừng sớm (check `done_flag`).

### Bài test bắt buộc

**Test 1 — Found:** Vector 100,000 ints (0–99999). Tìm 42,000. Phải return iterator tới element = 42,000.

**Test 2 — Not found:** Tìm -1. Phải return `last`.

**Test 3 — Early termination performance:** Vector 10,000,000 ints. Match ở index 100. Parallel_find phải hoàn thành **nhanh hơn đáng kể** so với linear search toàn bộ range (vì các thread khác dừng sớm).

**Test 4 — Multiple matches:** Vector chứa nhiều copies của match value. Phải return **một** valid iterator (bất kỳ match nào).

**Test 5 — Exception in worker:** Custom comparator throw khi gặp giá trị đặc biệt. Exception phải propagate, không terminate.

### Debug checkpoint

- **Version 1 critical race:** Nếu dùng `done.store(true)` thay vì `done.exchange(true)`, hai threads có thể cùng thấy `done == false`, cả hai gọi `set_value` → thread thứ hai throw `std::future_error` → unhandled → terminate.
- `result.set_exception(...)` cũng có thể throw nếu promise đã set → **phải** wrap trong try/catch.
- Version 1: join scope phải đóng **trước** khi check `done_flag` — nếu check trước join, thread có thể chưa set done.
- Version 2: `async_result.get()` phải được gọi **sau** direct search — nếu gọi trước, block cho đến khi async hoàn thành, mất early termination benefit.

### Câu hỏi phân tích

1. `done.exchange(true)` là read-modify-write atomic — tại sao `done.store(true)` không đủ? Trace interleaving 2 threads cùng tìm thấy match.
2. Version 1 dùng `std::promise`, version 2 dùng return value + `std::future` từ `std::async`. So sánh complexity và robustness.
3. Parallel find có thể return match ở **cuối** range dù có match ở **đầu** — tại sao? Điều này khác gì với `std::find` sequential?
4. Early termination bằng `atomic<bool>` thêm overhead mỗi iteration (atomic load). Khi nào overhead này lớn hơn benefit? (Hint: khi `f` mỗi element rất nhanh.)
5. Nếu dùng `std::stop_token` (C++20) thay vì `atomic<bool>`, thiết kế thay đổi thế nào?

---

## Bài 6 🔴 — `parallel_partial_sum`: Forward propagation với promise chain

### Đặc tả hành vi

Viết function template `parallel_partial_sum` — tính running total in-place cho range `[first, last)`.

```
template <typename Iterator>
void parallel_partial_sum(Iterator first, Iterator last);
```

Ví dụ: input `{1, 2, 3, 4, 5, 6, 7, 8, 9}` → output `{1, 3, 6, 10, 15, 21, 28, 36, 45}`.

### Thuật toán (Forward Propagation)

1. Chia range thành `num_threads` chunks.
2. Mỗi chunk: tính `std::partial_sum` nội bộ.
3. Mỗi chunk (trừ chunk đầu) phải **chờ** giá trị cuối cùng của chunk trước (qua `std::future`), rồi cộng giá trị đó vào tất cả elements trong chunk.
4. Sau khi cộng xong, set giá trị cuối cùng (đã cập nhật) vào `std::promise` để chunk tiếp theo nhận.

### Ràng buộc cứng

- Dùng `std::promise<value_type>` chain: `end_values[i]` là promise cho giá trị cuối chunk `i`. `previous_end_values[i]` là future từ `end_values[i-1]`.
- Dùng `join_threads`.
- **process_chunk** là callable object nhận: `(Iterator begin, Iterator last, std::future<value_type>* prev, std::promise<value_type>* end)`.
- `begin` tới `last` inclusive — `last` là phần tử cuối, **không phải** one-past-end.
- Exception trên worker thread → set vào promise → propagate tới chunk tiếp theo qua `future::get()`.
- **Không dùng `std::async`** — synchronization giữa chunks qua promise/future chain không phù hợp với async model.

### Invariant cần giữ

- Sau khi hoàn thành, mỗi element `result[i] = sum(original[0..i])`.
- Chunk `k` không bắt đầu forward propagation cho đến khi chunk `k-1` đã gửi giá trị cuối.
- **Chunk đầu** set promise **ngay** sau khi partial_sum xong (không chờ ai).
- **Chunk giữa** cập nhật phần tử cuối **trước**, set promise cho chunk tiếp, **rồi** mới cập nhật các phần tử còn lại — maximizing pipeline parallelism.

### Bài test bắt buộc

**Test 1 — Correctness small:** `{1,2,3,4,5,6,7,8,9}` → `{1,3,6,10,15,21,28,36,45}`. So sánh với `std::partial_sum`.

**Test 2 — Correctness large:** Vector 100,000 ints (tất cả = 1). Mỗi element kết quả phải bằng `index + 1`.

**Test 3 — Single thread:** Vector 10 ints (dưới min_per_thread). Vẫn đúng.

**Test 4 — Exception propagation:** Custom type có `operator+=` throw khi overflow. Exception phải propagate tới caller.

### Debug checkpoint

- Iterator range: `begin` tới `last` **inclusive** (khác convention thông thường). Khi gọi `std::partial_sum(begin, end, begin)` với `end = last + 1`.
- Thứ tự trong process_chunk **quan trọng**: cập nhật `*last` trước → set promise → `for_each(begin, last, ...)` cập nhật phần còn lại. Nếu đảo thứ tự → chunk tiếp phải chờ lâu hơn.
- `previous_end_values` push_back **bên trong** spawn loop — push **sau** spawn thread → race nếu thread đọc future trước khi push? Không — thread đọc `previous_end_values[i-1]`, push ở iteration `i`.
- Chunk cuối (main thread): `previous_end_value` pointer không null (trừ khi single chunk), nhưng `end_value` pointer là nullptr (không ai chờ).
- `future::get()` trả về **reference** tới stored value — lấy `value_type& addend` để tránh copy.

### Câu hỏi phân tích

1. Tại sao cập nhật `*last` trước rồi mới cập nhật phần còn lại? Trace timeline: chunk 0 xong → set promise → chunk 1 nhận → chunk 1 cập nhật last → set promise → chunk 2 nhận... Nếu cập nhật tất cả trước khi set promise, pipeline stall bao lâu?
2. Forward propagation approach là O(N) total work — nhưng parallelism bị giới hạn bởi chain dependency. Với k chunks, phần nào là serial? Amdahl's law áp dụng thế nào?
3. So sánh forward propagation với pairwise doubling (O(N log N) work, O(log N) parallel steps). Khi nào mỗi approach tốt hơn?
4. Exception propagation qua promise chain: nếu chunk 2 throw, chunk 3 nhận exception qua `future::get()`. Chunk 3 catch và set vào promise của nó → chunk 4 cũng nhận exception. Đây là behavior mong muốn hay nên dừng ở chunk 3?

---

# PHẦN C — Performance & Data Layout

---

## Bài 7 🟢 — False Sharing Benchmark: Đo + Sửa + So sánh

### Đặc tả hành vi

Viết benchmark program đo impact của false sharing khi nhiều threads cùng ghi vào các slot liền kề trong mảng.

Ba versions:

1. **Naive:** `long long counters[NUM_THREADS]` — các slot nằm cùng cache line.
2. **Padded:** Mỗi slot được align tới `std::hardware_destructive_interference_size` (hoặc `alignas(64)` nếu compiler chưa hỗ trợ).
3. **Local-then-merge:** Mỗi thread dùng local variable, cộng vào shared array chỉ một lần cuối.

Interface:

```
struct NaiveCounters { long long values[NUM_THREADS]; };

struct PaddedCounter {
    alignas(64) long long value = 0;
};
struct PaddedCounters { PaddedCounter values[NUM_THREADS]; };

void benchmark(const std::string& label, /* function that runs all threads */);
```

Mỗi thread increment slot riêng 50,000,000 lần. Đo thời gian cho mỗi version.

### Ràng buộc cứng

- `NUM_THREADS` = `std::thread::hardware_concurrency()` (hoặc ít nhất 4).
- Mỗi thread **chỉ** ghi vào slot riêng — không cần atomic, không cần mutex (no data race).
- Benchmark đo wall-clock time bằng `std::chrono::high_resolution_clock`.
- In kết quả: label, time (ms), sum (verify correctness).
- Chạy mỗi version ít nhất 3 lần, lấy giá trị trung bình.

### Invariant cần giữ

- Sum = `NUM_THREADS * ITERATIONS` cho tất cả versions.
- Padded version **nhanh hơn** naive trên multi-core system.
- Local-then-merge **nhanh nhất** (hoặc bằng padded).

### Bài test bắt buộc

**Test 1 — Correctness:** Sum đúng cho cả 3 versions.

**Test 2 — Performance ranking:** Naive ≥ Padded ≥ Local (thời gian). In ratio.

**Test 3 — Vary thread count:** Chạy với 1, 2, 4, 8 threads (hoặc max hardware). False sharing impact tăng theo thread count.

### Debug checkpoint

- `alignas(64)` trên struct member — verify bằng `static_assert(sizeof(PaddedCounter) >= 64)`.
- Nếu `std::hardware_destructive_interference_size` không available (pre-C++17 hoặc compiler cụ thể): `constexpr std::size_t CACHE_LINE = 64;` và dùng `alignas(CACHE_LINE)`.
- Compiler optimization: `-O2` có thể optimize away increment loop. Dùng `volatile` hoặc `benchmark::DoNotOptimize` pattern, hoặc verify sum cuối cùng.

### Câu hỏi phân tích

1. False sharing xảy ra ở tầng hardware — giải thích cơ chế: cache line ownership, MESI protocol (Modified/Exclusive/Shared/Invalid), cache line transfer giữa cores.
2. Tại sao mỗi thread chỉ ghi slot riêng nhưng vẫn bị ảnh hưởng? Thread A ghi `values[0]`, thread B ghi `values[1]` — nhưng cả hai nằm trên cùng cache line → mỗi write invalidate cache line của core kia.
3. `std::hardware_destructive_interference_size` vs `std::hardware_constructive_interference_size` — khi nào dùng cái nào? (Hint: "destructive" = tránh sharing, "constructive" = muốn cùng cache line.)
4. Local-then-merge approach: trade-off gì? Nếu thread cần **đọc** giá trị hiện tại của slot khác, approach này còn áp dụng được không?
5. Nếu thêm `std::atomic<long long>` cho mỗi slot (với `memory_order_relaxed`), false sharing có hết không? Tại sao? (Hint: atomic vẫn ghi vào cùng cache line.)

---

## Bài 8 🟡 — Cache-Aware Matrix Multiply: So sánh Row vs Column vs Block Division

### Đặc tả hành vi

Viết parallel matrix multiplication `C = A × B` cho square matrices, so sánh ba strategies chia work giữa threads:

```
template <typename T>
class Matrix {
public:
    Matrix(size_t rows, size_t cols);
    T& operator()(size_t r, size_t c);
    T const& operator()(size_t r, size_t c) const;
    size_t rows() const;
    size_t cols() const;
private:
    std::vector<T> data_;  // row-major layout
    size_t rows_, cols_;
};

Matrix<double> parallel_multiply_by_rows(Matrix<double> const& A, Matrix<double> const& B);
Matrix<double> parallel_multiply_by_cols(Matrix<double> const& A, Matrix<double> const& B);
Matrix<double> parallel_multiply_by_blocks(Matrix<double> const& A, Matrix<double> const& B);
```

Ba strategies:
1. **By rows:** Mỗi thread tính một nhóm rows liên tiếp của C.
2. **By columns:** Mỗi thread tính một nhóm columns liên tiếp của C.
3. **By blocks:** Mỗi thread tính một rectangular block của C.

### Ràng buộc cứng

- Matrix stored **row-major**: `data_[r * cols_ + c]`.
- Dùng `std::thread` + `join_threads` hoặc `std::async`.
- Số threads = `std::thread::hardware_concurrency()`.
- Kích thước matrix cho benchmark: ít nhất 500×500.
- Kết quả cả 3 strategies phải **giống nhau** (correctness) — so sánh element-by-element.

### Invariant cần giữ

- `C(i,j) = sum(A(i,k) * B(k,j), k=0..N-1)`.
- Mỗi element `C(i,j)` được tính bởi **đúng một** thread.
- Không data race: các threads ghi vào các vùng không overlap của C.

### Bài test bắt buộc

**Test 1 — Correctness:** Matrix 10×10, giá trị nhỏ. So sánh 3 strategies với nhân ma trận sequential naive. Kết quả phải khớp.

**Test 2 — Performance ranking:** Matrix 500×500 (hoặc lớn hơn). Đo thời gian 3 strategies. In ranking.

**Test 3 — False sharing analysis:** Strategy by_cols: threads ghi vào cùng row nhưng khác column — các element liền kề có thể nằm cùng cache line. Strategy by_rows: threads ghi vào rows khác nhau — ít false sharing hơn. Verify bằng timing.

### Debug checkpoint

- By rows: thread `t` tính rows `[t * chunk, (t+1) * chunk)`. Read pattern: toàn bộ row A[i,:] + từng column B[:,j]. Write: rows liên tiếp → contiguous memory → tốt.
- By cols: write pattern: columns liên tiếp → **không** contiguous trong row-major → false sharing giữa threads ghi cùng row.
- By blocks: read B chỉ cần columns trong block range, read A chỉ cần rows trong block range → ít cache pressure hơn by_rows.

### Câu hỏi phân tích

1. Trong row-major layout, tại sao division by rows thường nhanh hơn by columns? Phân tích read pattern và write pattern cho mỗi strategy.
2. Division by blocks giảm **read footprint** — giải thích bằng con số: matrix 1000×1000, 4 threads. By_rows: mỗi thread đọc bao nhiêu elements? By_blocks: bao nhiêu?
3. False sharing cụ thể xảy ra ở đâu trong by_columns strategy? Trace: thread 0 ghi `C(0,0)`, thread 1 ghi `C(0,250)` — cùng cache line hay không? (Assume cache line 64 bytes, `sizeof(double) = 8`.)
4. Nếu matrix stored **column-major**, ranking của 3 strategies thay đổi thế nào?

---

## Bài 9 🟡 — Pipeline Pattern: Multi-stage Image Processing

### Đặc tả hành vi

Viết pipeline 3 stages xử lý "ảnh" (simulated), mỗi stage chạy trên thread riêng, giao tiếp qua blocking queues.

```
// Simulated image = vector of pixels
using Image = std::vector<uint8_t>;

// Pipeline stages
Image stage1_load(int image_id);           // simulate I/O: sleep + generate data
Image stage2_process(Image img);            // simulate CPU work: transform pixels
void  stage3_save(Image img, int image_id); // simulate I/O: sleep + "save"

class Pipeline {
public:
    Pipeline(size_t queue_capacity);
    void run(int num_images);  // process num_images through pipeline
    // Stats
    size_t images_processed() const;
};
```

Bên trong `Pipeline::run`:
- Thread 1 (loader): gọi `stage1_load` cho mỗi image, push vào `queue_1_2`.
- Thread 2 (processor): pop từ `queue_1_2`, gọi `stage2_process`, push vào `queue_2_3`.
- Thread 3 (saver): pop từ `queue_2_3`, gọi `stage3_save`.
- Khi loader xong tất cả images: shutdown `queue_1_2`. Khi processor thấy shutdown + queue rỗng: shutdown `queue_2_3`. Khi saver thấy shutdown: exit.

### Ràng buộc cứng

- Dùng `bounded_queue<T>` tự viết (hoặc simplified version) cho inter-stage communication.
- Queue capacity configurable (ảnh hưởng pipeline behavior).
- Mỗi stage chạy trên **đúng 1 thread** (pipeline pattern, không data-parallel).
- Pipeline phải **shutdown gracefully** — không thread nào bị block mãi.
- Simulate timing: stage1 sleep 3ms, stage2 sleep 10ms, stage3 sleep 3ms.

### Invariant cần giữ

- Mỗi image đi qua đúng 3 stages theo thứ tự.
- Không image nào bị mất hoặc xử lý hai lần.
- Pipeline throughput bottleneck tại stage chậm nhất (stage2 = 10ms → ~100 images/sec).
- Tổng thời gian cho N images ≈ max(stage_times) × N + pipeline startup latency (N đủ lớn).

### Bài test bắt buộc

**Test 1 — Correctness:** Process 20 images. Verify `images_processed() == 20`.

**Test 2 — Throughput:** Process 100 images. Tổng thời gian phải gần `100 × 10ms = 1 second` (bottleneck stage2), **không phải** `100 × (3+10+3) = 1.6 seconds` (sequential).

**Test 3 — Graceful shutdown:** Process 10 images. Tất cả threads phải join sạch, không hang.

**Test 4 — Small queue capacity:** Capacity = 1. Vẫn đúng, nhưng throughput giảm (loader bị block thường xuyên).

### Debug checkpoint

- Shutdown sequencing: loader finish → push sentinel / shutdown queue_1_2 → processor nhận sentinel → shutdown queue_2_3 → saver nhận sentinel → exit.
- Nếu dùng `bounded_queue::shutdown()` thay vì sentinel: processor cần handle `queue_shutdown` exception hoặc check return value.
- Queue capacity quá nhỏ (= 1): loader block mỗi lần push vì processor chưa pop xong → pipeline degrades toward sequential. Queue capacity quá lớn: memory waste, latency vẫn bounded bởi stage2.

### Câu hỏi phân tích

1. Pipeline vs data-parallel: nếu dùng data-parallel (chia 100 images cho 3 threads, mỗi thread xử lý full pipeline cho 33 images), throughput thay đổi thế nào? Trade-off?
2. Tại sao pipeline pattern phù hợp khi stage times **khác nhau nhiều**? Nếu tất cả stages mất cùng thời gian, pipeline có lợi thế gì so với data-parallel?
3. Queue capacity = C: tối đa bao nhiêu images đang "in-flight" (đang được xử lý hoặc chờ) tại một thời điểm? Nó ảnh hưởng latency và throughput thế nào?
4. Mở rộng: nếu stage2 là bottleneck, ta có thể chạy **nhiều thread** cho stage2 (worker pool). Thiết kế thay đổi thế nào? Queue topology?

---

# PHẦN D — Synchronization Primitive

---

## Bài 10 🔴 — `simple_barrier`: Barrier class cho lockstep synchronization

### Đặc tả hành vi

Viết class `simple_barrier` — synchronization primitive cho phép N threads đợi nhau tại một điểm trước khi tất cả cùng tiếp tục.

Interface:

```
class simple_barrier {
public:
    explicit simple_barrier(unsigned count);

    void wait();          // block cho đến khi tất cả count threads đã gọi wait
    void done_waiting();  // thread rời barrier vĩnh viễn, giảm count cho rounds sau
};
```

Hành vi `wait()`:
- Mỗi thread gọi `wait()` giảm `spaces` đi 1.
- Thread cuối cùng (spaces về 0): reset `spaces = count`, tăng `generation`, unblock tất cả.
- Các thread khác: spin-wait (hoặc yield) cho đến khi `generation` thay đổi.
- Barrier **reusable** — sau khi tất cả threads vượt qua, barrier reset cho round tiếp theo.

Hành vi `done_waiting()`:
- Giảm `count` (thread này sẽ không tham gia rounds sau).
- Giảm `spaces` (tương tự wait, nhưng không wait).
- Nếu thread này là cuối cùng: reset `spaces` + tăng `generation`.

### Ràng buộc cứng

- `count`, `spaces`, `generation` đều là `std::atomic<unsigned>`.
- `wait()` dùng spin-wait với `std::this_thread::yield()` — không dùng condition variable.
- Tối đa `count` threads gọi `wait()` đồng thời — nếu nhiều hơn → undefined behavior.
- `done_waiting()` phải đúng semantics: giảm cả count lẫn spaces atomically enough.

### Invariant cần giữ

- Tất cả threads đến barrier trước khi bất kỳ thread nào vượt qua (lockstep).
- Barrier reusable: round 1 xong → round 2 bắt đầu → ... Round k xong → round k+1.
- Sau `done_waiting()`: thread rời barrier, các rounds sau chỉ cần `count - 1` threads.
- Không deadlock: nếu đủ threads gọi `wait()` hoặc `done_waiting()`, tất cả eventually proceed.

### Bài test bắt buộc

**Test 1 — Basic lockstep:** 4 threads, 5 rounds. Mỗi round: mỗi thread ghi `round_id` vào `results[thread_id][round_id]`, rồi `barrier.wait()`. Sau khi join: verify mỗi round hoàn thành trước khi round tiếp bắt đầu (tất cả threads ghi cùng round_id trước khi bất kỳ thread nào ghi round_id + 1).

**Test 2 — done_waiting:** 4 threads, nhưng thread 0 gọi `done_waiting()` sau round 2. Rounds 3, 4 chỉ cần 3 threads. Verify: không deadlock, 3 threads hoàn thành rounds 3–4.

**Test 3 — Stress test:** 8 threads, 100 rounds. Mỗi round: increment shared `std::atomic<int>`. Sau tất cả rounds: counter = 8 × 100 = 800 (với lockstep đảm bảo mỗi round tăng đúng 8).

### Debug checkpoint

- Race giữa `--spaces` và check `== 0`: dùng `fetch_sub(1)` → return giá trị trước khi trừ. Nếu giá trị trước = 1 → sau trừ = 0 → thread này là cuối cùng.
- `generation` phải tăng **sau** reset `spaces` — nếu tăng trước: thread khác thấy generation mới, vượt qua, gọi `wait()` round tiếp, `--spaces` xảy ra **trước** reset → spaces underflow.
- `done_waiting()` giảm `count` trước `spaces` — nếu giảm `spaces` trước rồi spaces về 0, reset spaces bằng count cũ (chưa giảm) → lần sau cần count threads thay vì count-1.
- Spin-wait: `while (generation.load() == my_generation) yield();` — nếu không `yield()`, busy-wait 100% CPU.

### Câu hỏi phân tích

1. Tại sao barrier cần `generation` counter thay vì đơn giản reset `spaces` rồi return? Trace interleaving: thread A vượt barrier, chạy nhanh, gọi `wait()` lần 2 **trước** thread B vượt barrier lần 1. Không có generation → A bị stuck.
2. Barrier dùng spin-wait — khi nào spin-wait hợp lý? Khi nào nên dùng condition variable thay thế? (Hint: expected wait time, CPU utilization.)
3. `done_waiting()` cần sửa cả `count` lẫn `spaces` — hai atomic operations riêng rẽ. Có race condition nào giữa hai operations? Trace: thread A gọi `done_waiting()`, giảm `count`, chưa giảm `spaces`. Thread B gọi `wait()`, giảm `spaces` về 0, reset `spaces = count` (đã giảm). OK?
4. So sánh `simple_barrier` với `std::barrier` (C++20): features nào thiếu? (`std::barrier` có completion function, `arrive_and_drop`, `arrive_and_wait`.)

---

## Bài 11 🔴 — `parallel_partial_sum_pairwise`: Pairwise doubling dùng barrier

### Đặc tả hành vi

Viết version thứ hai của `parallel_partial_sum` dùng pairwise doubling algorithm + `simple_barrier` (Bài 10).

```
template <typename Iterator>
void parallel_partial_sum_pairwise(Iterator first, Iterator last);
```

### Thuật toán (Pairwise Doubling)

Cho N elements, dùng N threads (1 thread per element). Lặp qua log₂(N) rounds:
- Round 0 (stride=1): `result[i] += result[i-1]` (nếu `i >= 1`).
- Round 1 (stride=2): `result[i] += result[i-2]` (nếu `i >= 2`).
- Round 2 (stride=4): `result[i] += result[i-4]` (nếu `i >= 4`).
- ...
- Round k (stride=2^k): `result[i] += result[i-2^k]` (nếu `i >= 2^k`).

Sau log₂(N) rounds, kết quả là partial sum.

Mỗi round: tất cả threads đọc + cộng đồng thời, rồi **barrier wait** trước round tiếp.

### Double Buffering

Để tránh race (thread đọc element đang bị thread khác ghi trong cùng round):
- Dùng **buffer** song song với original range.
- Round chẵn: đọc từ original, ghi vào buffer.
- Round lẻ: đọc từ buffer, ghi vào original.
- Sau round cuối: nếu kết quả ở buffer → copy về original.

### Ràng buộc cứng

- Dùng `simple_barrier` (Bài 10) cho synchronization giữa rounds.
- Số threads = số elements (hoặc giới hạn hợp lý). Mỗi thread xử lý element `i`.
- Khi stride > i: thread gọi `barrier.done_waiting()` (rời barrier vĩnh viễn).
- Dùng `join_threads` cho exception safety.
- Buffer = `std::vector<value_type>` cùng kích thước range.

### Invariant cần giữ

- Sau round k: elements `0..2^(k+1)-1` có giá trị partial sum đúng.
- Barrier đảm bảo tất cả threads hoàn thành round k trước khi bất kỳ thread nào bắt đầu round k+1.
- Thread rời sớm (done_waiting) không ảnh hưởng correctness — giá trị element đó đã final.
- Kết quả cuối cùng phải nằm trong original range (không phải buffer).

### Bài test bắt buộc

**Test 1 — Correctness small:** `{1,2,3,4,5,6,7,8}` → `{1,3,6,10,15,21,28,36}`. (8 elements, 3 rounds.)

**Test 2 — Correctness non-power-of-2:** `{1,2,3,4,5,6,7,8,9}` → `{1,3,6,10,15,21,28,36,45}`.

**Test 3 — done_waiting correctness:** Verify: thread 0 calls `done_waiting` sau round 0 (stride 1 > 0, nên thread 0 xong ngay). Barrier count giảm mỗi round. Không deadlock.

**Test 4 — Compare with forward propagation:** So sánh kết quả với Bài 6. Phải khớp bit-for-bit.

### Debug checkpoint

- Double buffering: `source = (step % 2) ? buffer[i] : original[i]`. `dest = (step % 2) ? original[i] : buffer[i]`. `addend = (step % 2) ? buffer[i - stride] : original[i - stride]`. `dest = source + addend`.
- Nếu quên barrier → thread nhanh đọc giá trị chưa cập nhật từ thread chậm → kết quả sai.
- `done_waiting()` phải được gọi **sau** ghi kết quả cuối cùng — nếu gọi trước khi ghi, thread khác ở round tiếp có thể đọc giá trị cũ.
- Sau loop: check `update_source` flag để biết kết quả nằm ở buffer hay original. Nếu ở buffer → copy về.

### Câu hỏi phân tích

1. O(N log N) total operations vs O(N) của forward propagation — tại sao pairwise vẫn có thể **nhanh hơn** trên hệ thống massively parallel?
2. Barrier spin-wait tiêu tốn CPU — nếu N = 10,000 nhưng hardware chỉ 8 cores, N threads sẽ oversubscribe nặng. Giải pháp thực tế? (Hint: mỗi thread xử lý N/k elements thay vì 1.)
3. Double buffering tăng memory footprint gấp đôi. Có cách nào tránh double buffer mà vẫn correct? (Hint: nếu tất cả threads sync bằng barrier, và ghi không overlap với đọc ở cùng round...)
4. So sánh `simple_barrier` spin-wait vs `std::barrier` (C++20) completion function cho use case này. `std::barrier` completion function chạy ở thread cuối cùng — có thể dùng để swap buffer pointers thay vì double-buffer logic trong mỗi thread.

---

# Thứ tự làm đề xuất

```
Bài 1  (join_threads RAII — foundation cho mọi bài sau)
    ↓
Bài 2  (parallel_accumulate manual — core pattern)
    ↓
Bài 3  (parallel_accumulate async — so sánh)
    ↓
Bài 4  (parallel_for_each — cả 2 versions)
    ↓
Bài 7  (False sharing benchmark — pause, đo, hiểu performance)
    ↓
Bài 8  (Matrix multiply — apply cache awareness)
    ↓
Bài 5  (parallel_find — hard: early termination + promise)
    ↓
Bài 9  (Pipeline pattern — different concurrency model)
    ↓
Bài 6  (parallel_partial_sum forward — hard: promise chain)
    ↓
Bài 10 (Barrier class — synchronization primitive)
    ↓
Bài 11 (Partial sum pairwise — synthesis: barrier + double buffer)
```

---

# Checklist "done" cho mỗi bài

- [ ] Compile sạch với `-Wall -Wextra -Wpedantic`
- [ ] Chạy sạch với `-fsanitize=thread` (không data race report)
- [ ] Chạy sạch với `-fsanitize=address` (không memory error)
- [ ] Tất cả test cases pass, bao gồm exception paths
- [ ] Benchmark results make sense (parallel nhanh hơn sequential trên multi-core)
- [ ] Có thể trả lời tất cả câu hỏi phân tích bằng lời, không nhìn code
- [ ] Có thể trace worst-case interleaving mà thiết kế vẫn đúng
- [ ] Giải thích được tại sao exception safety approach này tốt hơn naive
- [ ] Hiểu performance implications: false sharing, cache locality, oversubscription
