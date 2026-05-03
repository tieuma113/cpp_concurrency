# Bài tập code — C++ Concurrency in Action, Chapter 9: Advanced Thread Management

Tài liệu nguồn: **Chapter 9 — Advanced Thread Management** của *C++ Concurrency in Action* (Anthony Williams). Bao gồm: thread pool evolution (simple → waitable → run_pending_task → thread-local queues → work stealing), `function_wrapper` type erasure, và thread interruption mechanism (`interrupt_flag`, `interruptible_thread`, `interruption_point`, `interruptible_wait` cho condition variable / future).

Toolchain: `g++ -std=c++17 -pthread -g -fsanitize=thread`

Phụ thuộc từ các chapter trước:
- `join_threads` (Chapter 8, Bài 1)
- `threadsafe_queue<T>` (Chapter 6) — dùng cho global work queue trong thread pool

---

# PHẦN A — Thread Pool Evolution

---

## Bài 1 🟢 — `function_wrapper`: Type-erased move-only callable

### Đặc tả hành vi

Viết class `function_wrapper` — thay thế `std::function<void()>` cho trường hợp callable object **chỉ movable, không copyable** (ví dụ `std::packaged_task`).

Interface:

```
class function_wrapper {
public:
    function_wrapper() = default;

    template <typename F>
    function_wrapper(F&& f);

    function_wrapper(function_wrapper&& other) noexcept;
    function_wrapper& operator=(function_wrapper&& other) noexcept;

    function_wrapper(const function_wrapper&) = delete;
    function_wrapper& operator=(const function_wrapper&) = delete;

    void operator()();
};
```

Hành vi:
- Construct từ bất kỳ callable `F` nào nhận 0 argument, return void.
- Gọi `operator()` → invoke stored callable.
- Move-only: copy bị delete.
- Default-constructed wrapper: gọi `operator()` là undefined (không cần check).

### Thiết kế nội bộ (Type Erasure)

- `impl_base`: abstract base class với `virtual void call() = 0` + virtual destructor.
- `impl_type<F>`: concrete class giữ `F f`, `call()` gọi `f()`.
- `function_wrapper` giữ `std::unique_ptr<impl_base> impl`.
- Constructor: `impl = make_unique<impl_type<F>>(std::forward<F>(f))`.

### Ràng buộc cứng

- `F` phải được **move** vào `impl_type`, không copy.
- `function_wrapper` move constructor/assignment transfer ownership qua `std::unique_ptr`.
- Không dùng `std::function` bên trong.
- Phải compile với `std::packaged_task<int()>` làm `F`.

### Bài test bắt buộc

**Test 1 — Lambda:** `function_wrapper fw([]{ std::cout << "hello"; }); fw();` → in "hello".

**Test 2 — packaged_task:** `std::packaged_task<int()> pt([]{ return 42; }); auto f = pt.get_future(); function_wrapper fw(std::move(pt)); fw(); assert(f.get() == 42);`

**Test 3 — Move semantics:** `function_wrapper a([]{}); function_wrapper b(std::move(a)); b();` — OK. `a()` — undefined/crash (moved-from).

**Test 4 — Compile failure:** `function_wrapper a([]{}); function_wrapper b = a;` — phải **không compile**.

### Debug checkpoint

- `std::function<void()>` không chấp nhận `std::packaged_task` vì `packaged_task` không copyable — đây là **toàn bộ lý do tồn tại** của `function_wrapper`.
- `impl_type` constructor phải nhận `F&&` và `std::move` — nếu nhận `F` by value thì thêm một move thừa nhưng vẫn đúng.
- `operator()` chỉ cần `impl->call()` — không cần null check trong production (caller's responsibility).

### Câu hỏi phân tích

1. Tại sao `std::function` yêu cầu stored callable phải copyable? (Hint: `std::function` bản thân là copyable.)
2. Type erasure pattern ở đây: `function_wrapper` không biết `F` là gì — chỉ biết gọi `call()`. So sánh với template approach `template<typename F> class wrapper { F f; }` — trade-off?
3. Virtual dispatch overhead: mỗi `operator()` gọi virtual function. Khi nào overhead này acceptable, khi nào không?
4. Nếu muốn hỗ trợ callable có return type khác void (ví dụ `function_wrapper<int()>`), thiết kế thay đổi thế nào?

---

## Bài 2 🟢 — `simple_thread_pool`: Fire-and-forget thread pool

### Đặc tả hành vi

Viết class `simple_thread_pool` — thread pool cơ bản nhất: submit task, worker threads tự chạy, không chờ kết quả.

Interface:

```
class simple_thread_pool {
public:
    simple_thread_pool();   // spawn hardware_concurrency() threads
    ~simple_thread_pool();  // signal done, join all

    template <typename F>
    void submit(F f);       // fire-and-forget
};
```

Hành vi:
- Constructor: tạo `std::thread::hardware_concurrency()` worker threads.
- Worker thread loop: `while(!done) { try_pop task → run; else yield; }`
- Destructor: set `done = true`, join tất cả threads.
- `submit(f)`: push `std::function<void()>(f)` vào shared queue.

### Ràng buộc cứng

- Dùng `threadsafe_queue<std::function<void()>>` cho work queue.
- Dùng `join_threads` (Chapter 8) cho exception safety trong constructor.
- `std::atomic<bool> done` cho shutdown signal.
- Constructor phải handle exception khi spawn threads: set `done = true` + rethrow (threads đã spawn sẽ exit loop vì `done`).
- **Không dùng `function_wrapper` ở bài này** — chỉ `std::function<void()>`.

### Invariant cần giữ

- Mỗi task submitted được execute **đúng một lần** (nếu pool chưa destroyed).
- Khi destructor chạy: tasks đang chờ trong queue **có thể bị bỏ** (fire-and-forget semantics).
- Không data race trên shared state.

### Bài test bắt buộc

**Test 1 — Basic execution:** Submit 100 tasks, mỗi task increment `std::atomic<int>`. Sleep chờ. Counter phải = 100.

**Test 2 — Multiple submitters:** 4 external threads, mỗi thread submit 250 tasks. Counter phải = 1000.

**Test 3 — Destructor waits:** Submit 10 tasks sleep 50ms. Destroy pool. Tổng thời gian phải ≥ 50ms (destructor join).

**Test 4 — Exception in constructor:** Mock `std::thread::hardware_concurrency()` return 1000000 (unrealistic). Constructor phải throw, không leak threads.

### Debug checkpoint

- **Declaration order quan trọng:** `done` và `work_queue` phải declare **trước** `threads` vector, `threads` trước `joiner`. Destructor chạy ngược declaration order: `joiner` join threads trước → rồi `threads` destroyed → rồi `work_queue` destroyed. Nếu sai thứ tự: threads còn đang chạy khi queue bị destroy → UB.
- Worker loop: `if (work_queue.try_pop(task)) task(); else yield();` — nếu dùng `wait_and_pop` thay `try_pop`: destructor set `done = true` nhưng thread đang block trong `wait_and_pop` → deadlock.
- `submit` wrap `f` trong `std::function<void()>` — nếu `f` không void(), phải wrap trong lambda: `submit([f]{ f(); })`.

### Câu hỏi phân tích

1. Pool dùng `try_pop` + `yield` thay vì `wait_and_pop`. Trade-off: CPU waste (busy-yield) vs responsiveness. Khi nào mỗi approach phù hợp?
2. Destructor set `done = true` nhưng không drain queue — tasks còn lại bị bỏ. Đây có phải design flaw? Khi nào acceptable?
3. Nếu worker thread throw exception (task throw), thread đó die → pool mất 1 worker vĩnh viễn. Giải pháp?
4. Tại sao dùng `std::thread::hardware_concurrency()` cho số threads? Nếu tasks chủ yếu I/O-bound, số threads tối ưu thay đổi thế nào?

---

## Bài 3 🟡 — `waitable_thread_pool`: Submit trả về `std::future`

### Đặc tả hành vi

Mở rộng `simple_thread_pool` thành pool có thể **chờ kết quả** — `submit()` trả về `std::future<ResultType>`.

Interface:

```
class waitable_thread_pool {
public:
    waitable_thread_pool();
    ~waitable_thread_pool();

    template <typename F>
    std::future<typename std::invoke_result_t<F>> submit(F f);
};
```

Hành vi `submit`:
- Deduce `result_type = std::invoke_result_t<F>`.
- Tạo `std::packaged_task<result_type()>(std::move(f))`.
- Lấy `future` từ packaged_task.
- Push task vào queue (dưới dạng `function_wrapper`).
- Return `future`.

### Ràng buộc cứng

- **Phải dùng `function_wrapper`** (Bài 1) thay `std::function` — vì `std::packaged_task` không copyable.
- Work queue type: `threadsafe_queue<function_wrapper>`.
- Caller nhận `std::future` → có thể `get()` để chờ kết quả hoặc nhận exception.
- Worker thread try/catch **không cần** — exception tự capture vào packaged_task/future.

### Invariant cần giữ

- `future.get()` block cho đến khi task hoàn thành.
- Nếu task throw, `future.get()` rethrow exception.
- Mỗi task execute đúng một lần.
- Tasks trong queue bị bỏ khi pool destroy — futures của chúng sẽ throw `std::future_error` khi `get()` (broken promise).

### Bài test bắt buộc

**Test 1 — Return value:** `auto f = pool.submit([]{ return 42; }); assert(f.get() == 42);`

**Test 2 — Exception propagation:** `auto f = pool.submit([]() -> int { throw std::runtime_error("boom"); }); try { f.get(); } catch (std::runtime_error& e) { /* caught */ }`

**Test 3 — Parallel accumulate:** Dùng `waitable_thread_pool` viết `parallel_accumulate` tương tự listing 9.3. Vector 100,000 ints. Kết quả đúng.

**Test 4 — Multiple futures:** Submit 50 tasks trả về index. Collect tất cả futures. `get()` tất cả. Verify tất cả 50 giá trị đúng.

**Test 5 — Void task:** `auto f = pool.submit([]{ /* side effect */ }); f.get(); // should not throw`

### Debug checkpoint

- `submit` phải `std::move(task)` khi push vào queue — `packaged_task` không copyable.
- `std::invoke_result_t<F>` (C++17) thay `std::result_of<F()>::type` (deprecated).
- `packaged_task<result_type()>` — type argument là **function signature**, không phải return type.
- `function_wrapper` nhận `packaged_task` qua move — nếu `function_wrapper` constructor yêu cầu copy → compile error.

### Câu hỏi phân tích

1. `std::function<void()>` (Bài 2) vs `function_wrapper` (Bài 3) — tại sao chuyển sang `function_wrapper` là bắt buộc khi dùng `packaged_task`?
2. Caller giữ `std::future` — nếu caller destroy future mà không `get()`, task vẫn chạy? Kết quả đi đâu?
3. Pool destroy trước khi tất cả tasks chạy → `packaged_task` trong queue bị destroy → promise broken → future throw. Đây là behavior mong muốn? Có cách nào drain queue trước khi destroy?
4. So sánh approach này với `std::async` — thread pool reuse threads, `std::async` có thể spawn mới mỗi lần. Trade-off?

---

## Bài 4 🟡 — `run_pending_task()`: Thread pool cho tasks chờ tasks khác

### Đặc tả hành vi

Mở rộng `waitable_thread_pool` (Bài 3) thêm hàm `run_pending_task()` — cho phép thread đang chờ kết quả của task khác **tự chạy task từ queue** thay vì block idle.

Interface bổ sung:

```
class thread_pool {
public:
    // ... như Bài 3 ...

    void run_pending_task();
    // Nếu queue có task: pop + execute.
    // Nếu queue rỗng: yield.
};
```

**Use case chính:** Parallel quicksort. Task A submit task B vào pool, rồi chờ B xong. Nếu tất cả worker threads đang chờ → deadlock. Nhưng nếu thread chờ gọi `run_pending_task()` trong vòng lặp chờ → nó tự chạy tasks khác, phá vỡ deadlock.

### Ràng buộc cứng

- `run_pending_task()` phải **public** — cả external thread lẫn pool thread đều gọi được.
- Worker thread loop bây giờ gọi `run_pending_task()` thay vì inline logic.
- `run_pending_task()` **không block** — nếu không có task, return ngay (sau yield).

### Invariant cần giữ

- Tasks chờ tasks khác: không deadlock nếu waiting thread gọi `run_pending_task()` trong while loop.
- Task execution order: **không đảm bảo** — task mới submit có thể chạy trước task cũ.

### Bài test bắt buộc

**Test 1 — Parallel quicksort:** Viết `parallel_quick_sort<T>` dùng pool. `do_sort()` submit lower chunk vào pool, sort upper chunk trực tiếp. While chờ lower:
```cpp
while (new_lower.wait_for(0s) == std::future_status::timeout)
    pool.run_pending_task();
```
Sort `std::list<int>` 100,000 phần tử ngẫu nhiên. Verify sorted.

**Test 2 — No deadlock:** Pool 2 threads. Task A submit Task B, Task B submit Task C. Tất cả chờ nhau nhưng dùng `run_pending_task()`. Không deadlock.

**Test 3 — External thread runs tasks:** Main thread (không phải pool thread) gọi `run_pending_task()` — phải chạy task từ queue nếu có.

### Debug checkpoint

- Quicksort: `do_sort()` dùng `std::bind(&sorter::do_sort, this, std::move(chunk))` hoặc lambda capture `[this, chunk = std::move(chunk)]{ return do_sort(chunk); }`. `std::bind` + `std::move` cần cẩn thận — bind copy arguments mặc định.
- `run_pending_task()` = extract logic từ worker loop thành function riêng. Worker loop giờ chỉ: `while(!done) run_pending_task();`
- Nếu pool có N threads mà submit > N tasks đệ quy chờ nhau → chỉ hoạt động nếu **ít nhất 1 task** available trong queue khi mỗi thread gọi `run_pending_task()`.

### Câu hỏi phân tích

1. Tại sao simple thread pool (Bài 2, 3) gây deadlock với recursive tasks? Trace: 2 pool threads, task A submit B, task B submit C, cả A, B chờ. Pool hết thread → C chạy không bao giờ chạy.
2. `run_pending_task()` cho phép "work while waiting" — giải thích tại sao đây là pattern quan trọng trong thread pool design.
3. Nếu task trong `run_pending_task()` cũng submit + chờ recursive → stack depth tăng không giới hạn. Giải pháp?
4. So sánh approach này với `std::async(std::launch::deferred)` — deferred async cũng chạy task khi `get()` được gọi. Khác nhau?

---

## Bài 5 🔴 — Thread pool với thread-local queues

### Đặc tả hành vi

Mở rộng thread pool (Bài 4) thêm **per-thread local queue** — mỗi pool thread có queue riêng, giảm contention trên shared global queue.

Thiết kế:

```
class thread_pool {
    threadsafe_queue<function_wrapper> pool_work_queue;          // global queue

    static thread_local std::unique_ptr<local_queue_type> local_work_queue;
    // Mỗi pool thread có queue riêng, non-pool threads không có

    // ...
};
```

- `local_queue_type` = `std::queue<function_wrapper>` — plain queue, **không cần thread-safe** (chỉ 1 thread access).
- Pool thread: `submit()` push vào **local queue** (nếu có). Non-pool thread: push vào **global queue**.
- `run_pending_task()`: check local queue trước → rồi global queue → rồi yield.

### Ràng buộc cứng

- `thread_local` biến: `static thread_local std::unique_ptr<local_queue_type> local_work_queue`.
- Worker thread init: `local_work_queue.reset(new local_queue_type)` đầu `worker_thread()`.
- `submit`: `if (local_work_queue) local_work_queue->push(task); else pool_work_queue.push(task);`
- `run_pending_task`: local queue first → global queue second.
- `thread_local` variable phải **declare trong class**, **define ngoài class** (hoặc inline static trong C++17).

### Invariant cần giữ

- Non-pool threads (external callers) **không có** local queue → `local_work_queue == nullptr` → submit vào global queue.
- Pool threads submit vào local queue → **zero contention** nếu không có work stealing.
- Tất cả tasks vẫn được execute đúng một lần.

### Bài test bắt buộc

**Test 1 — Local queue usage:** Submit task từ pool thread (bên trong task đã submit) → task mới phải vào local queue, không vào global. Verify bằng counter: task chạy trên **cùng thread** với task đã submit nó (check `std::this_thread::get_id()`).

**Test 2 — External submit:** Main thread submit 100 tasks → tất cả vào global queue → pool threads pick up. Counter = 100.

**Test 3 — Quicksort still works:** Parallel quicksort từ Bài 4 vẫn hoạt động, không deadlock.

**Test 4 — Contention reduction:** Benchmark: submit 1,000,000 trivial tasks từ pool threads (recursive fan-out). So sánh thời gian với Bài 4 (global queue only). Local queue version phải nhanh hơn.

### Debug checkpoint

- `thread_local std::unique_ptr<local_queue_type>` — `unique_ptr` destructor tự clean up khi thread exit. Nếu dùng raw pointer → memory leak.
- `local_work_queue` check: **phải check nullptr** trước access — non-pool threads có `local_work_queue == nullptr`.
- Local queue là **plain `std::queue`**, không thread-safe. An toàn vì chỉ owning thread truy cập. Nhưng nếu implement work stealing (Bài 6) → phải thay đổi.
- Vấn đề imbalance: nếu 1 thread submit 10000 tasks vào local queue, các thread khác idle → throughput giảm. → Motivation cho work stealing.

### Câu hỏi phân tích

1. Tại sao local queue giảm contention? Global queue: mỗi `push`/`try_pop` lock mutex → N threads cạnh tranh. Local queue: zero contention cho `push` + `pop` trên cùng thread.
2. Task distribution imbalance: quicksort submit lower chunk → local queue → chỉ thread hiện tại thấy. Threads khác idle. Giải pháp?
3. `thread_local` trong C++: lifetime gắn với thread, mỗi thread có copy riêng. Nếu thread bị detach, `thread_local` object bị destroy khi nào?
4. Non-pool thread gọi `run_pending_task()`: check `local_work_queue` → null → fallback global queue. Đúng behavior? Có nên tạo temporary local queue cho non-pool thread?

---

## Bài 6 🔴 — `work_stealing_thread_pool`: Thread pool với work stealing

### Đặc tả hành vi

Mở rộng thread pool (Bài 5) thêm **work stealing** — thread idle có thể "ăn cắp" tasks từ queue của thread khác.

**Hai components:**

### Component 1: `work_stealing_queue`

```
class work_stealing_queue {
public:
    work_stealing_queue() = default;

    work_stealing_queue(const work_stealing_queue&) = delete;
    work_stealing_queue& operator=(const work_stealing_queue&) = delete;

    void push(function_wrapper data);       // push front (LIFO cho owner)
    bool try_pop(function_wrapper& res);    // pop front  (LIFO cho owner)
    bool try_steal(function_wrapper& res);  // pop back   (FIFO cho thief)
    bool empty() const;
};
```

Nội bộ: `std::deque<function_wrapper>` + `std::mutex`.
- Owner thread: `push` + `try_pop` từ **front** → LIFO (stack behavior cho cache locality).
- Thief thread: `try_steal` từ **back** → minimize contention (khác đầu với owner).

### Component 2: Thread pool với stealing

```
class work_stealing_thread_pool {
public:
    work_stealing_thread_pool();
    ~work_stealing_thread_pool();

    template <typename F>
    std::future<std::invoke_result_t<F>> submit(F f);

    void run_pending_task();

private:
    // 3 levels: local queue → global queue → steal from others
    bool pop_task_from_local_queue(task_type& task);
    bool pop_task_from_pool_queue(task_type& task);
    bool pop_task_from_other_thread_queue(task_type& task);
};
```

Hành vi `run_pending_task()`:
1. Try local queue (owner pop front).
2. Try global pool queue.
3. Try steal từ queue của thread khác (round-robin, bắt đầu từ `(my_index + 1) % N`).
4. Nếu tất cả fail → yield.

### Ràng buộc cứng

- **Mỗi pool thread** có 1 `work_stealing_queue` — pool quản lý trong `std::vector<std::unique_ptr<work_stealing_queue>>`.
- Thread biết index riêng qua `thread_local unsigned my_index`.
- Thread biết queue riêng qua `thread_local work_stealing_queue* local_work_queue`.
- Pool constructor: tạo tất cả queues **trước**, rồi spawn threads (thread cần queue pointer ngay khi start).
- `try_steal` iterate qua **tất cả** queues khác, dùng `(my_index + i + 1) % queues.size()` để tránh thundering herd.
- `work_stealing_queue` dùng `std::mutex` — work stealing hiếm, contention thấp.

### Invariant cần giữ

- Owner push/pop front → LIFO → recently pushed task chạy trước → **better cache locality** (data từ task vừa submit thường còn hot trong cache).
- Thief steal from back → tasks cũ nhất → minimize interference với owner.
- Mỗi task execute đúng 1 lần, bất kể local/global/stolen.
- Không deadlock: stealing không hold lock trên nhiều queue cùng lúc.

### Bài test bắt buộc

**Test 1 — Basic functionality:** Submit 10,000 tasks trả về index. Collect futures. `get()` tất cả. Verify tất cả 10,000 giá trị present.

**Test 2 — Work stealing triggers:** Pool 4 threads. Task 0 submit 1000 sub-tasks vào local queue. Threads 1–3 ban đầu idle → phải steal tasks từ thread 0. Verify: tasks chạy trên nhiều threads khác nhau (check `std::this_thread::get_id()` trong task).

**Test 3 — Parallel quicksort:** Quicksort list 500,000 phần tử. Verify sorted. Benchmark vs Bài 4 (no stealing) — stealing version phải nhanh hơn khi workload imbalanced.

**Test 4 — Stealing round-robin:** Submit tasks chỉ vào thread 0's queue. Observe: thread 1 steal trước (index `(0+1)%4 = 1`), rồi thread 2, rồi thread 3. Không phải tất cả steal từ thread 0 cùng lúc.

### Debug checkpoint

- `work_stealing_queue`: `push` push **front**, `try_pop` pop **front**, `try_steal` pop **back**. Nếu `try_steal` cũng pop front → owner và thief cạnh tranh cùng đầu → contention tăng.
- Pool constructor: tạo queues trước, spawn threads sau. Nếu đảo: thread access `queues[my_index]` khi vector chưa đầy → UB.
- `pop_task_from_other_thread_queue`: loop qua `queues.size()` threads, **skip bản thân** (`i != my_index`). Nếu tất cả empty → return false.
- LIFO cho owner, FIFO cho thief: quicksort submit lower chunk → local queue. Owner pop lower chunk (most recent) → process it → submit more. Thief steal oldest chunk (shallowest recursion) → larger chunk → more useful work.

### Câu hỏi phân tích

1. Tại sao LIFO cho owner, FIFO cho thief? Trace quicksort: recursive subdivision tạo task tree. Owner muốn depth-first (LIFO) để hoàn thành nhanh. Thief muốn breadth-first (FIFO) để lấy task lớn, tạo nhiều sub-tasks.
2. `work_stealing_queue` dùng `std::mutex` — khi nào contention xảy ra? (Chỉ khi thief steal cùng lúc owner push/pop.) Nếu dùng lock-free deque (Chase-Lev), benefit?
3. Round-robin stealing bắt đầu từ `(my_index + 1)` — tại sao không bắt đầu từ index 0? (Hint: N threads cùng idle cùng lúc, tất cả steal từ thread 0 → thundering herd.)
4. So sánh work-stealing pool với `std::async`: `std::async` cũng quản lý threads nhưng không expose work stealing. Trường hợp nào work-stealing pool tốt hơn?

---

# PHẦN B — Thread Interruption

---

## Bài 7 🟡 — `interrupt_flag` + `interruptible_thread`: Core interruption mechanism

### Đặc tả hành vi

Viết mechanism cho phép thread A yêu cầu thread B dừng — thread B kiểm tra tại **interruption points** và throw exception nếu bị interrupt.

**Component 1: `interrupt_flag`**

```
class interrupt_flag {
public:
    void set();
    bool is_set() const;
};
```

Mỗi thread có `thread_local interrupt_flag this_thread_interrupt_flag`.

**Component 2: `interruption_point()`**

```
void interruption_point();
// Nếu this_thread_interrupt_flag.is_set() → throw thread_interrupted{};
```

**Component 3: `thread_interrupted` exception**

```
class thread_interrupted : public std::exception {
public:
    const char* what() const noexcept override { return "thread interrupted"; }
};
```

**Component 4: `interruptible_thread`**

```
class interruptible_thread {
public:
    template <typename F>
    interruptible_thread(F f);

    void join();
    void detach();
    bool joinable() const;
    void interrupt();
};
```

Hành vi constructor:
- Tạo internal `std::thread` chạy wrapper lambda.
- Wrapper: set `this_thread_interrupt_flag` pointer vào `std::promise`, rồi gọi `f()`.
- Constructor wait `promise.get_future().get()` để nhận pointer tới thread's `interrupt_flag`.
- Wrapper catch `thread_interrupted` → swallow (thread exits gracefully).

Hành vi `interrupt()`:
- Gọi `flag->set()` (flag pointer nhận từ promise).
- Thread B sẽ throw tại `interruption_point()` tiếp theo.

### Ràng buộc cứng

- `interrupt_flag::flag` là `std::atomic<bool>`.
- `this_thread_interrupt_flag` là `thread_local` → mỗi thread có instance riêng.
- `interruptible_thread` constructor: lambda capture `f` by value, `std::promise& p` by reference. Promise set **trước** khi gọi `f()`. Constructor get future → get pointer. Dangling reference tới `p` safe vì constructor wait trên future → không return trước khi lambda done with `p`.
- Internal thread catch `thread_interrupted` → thread exits, không crash app.

### Bài test bắt buộc

**Test 1 — Basic interruption:** `interruptible_thread t([&]{ while(true) { interruption_point(); counter++; } }); sleep(100ms); t.interrupt(); t.join(); assert(counter > 0);`

**Test 2 — Not interrupted:** `interruptible_thread t([&]{ for(int i=0;i<100;i++) { interruption_point(); counter++; } }); t.join(); assert(counter == 100);` — không gọi interrupt → thread chạy hoàn thành.

**Test 3 — Interrupt before start:** `interruptible_thread t(f); t.interrupt();` gọi interrupt ngay → thread throw tại interruption_point đầu tiên.

**Test 4 — Multiple interruption points:** Thread có 3 phases, mỗi phase check `interruption_point()`. Interrupt giữa phase 2 → phase 3 không chạy.

### Debug checkpoint

- `std::promise` pass pointer tới `thread_local` variable — pointer valid chỉ khi thread còn sống. Nếu thread exit trước `interrupt()` → dangling pointer. Phải clear `flag` pointer hoặc check trước set.
- Lambda trong constructor: `[f, &p]` — `f` by value (thread owns copy), `p` by reference (used only during setup, before constructor returns).
- `thread_local interrupt_flag this_thread_interrupt_flag;` — global thread_local. Mỗi thread tự có instance. Không cần mutex.
- Worker thread: `try { f(); } catch (thread_interrupted&) {} ` — swallow exception. Nếu `f` throw exception khác → propagate → `std::terminate` (unhandled trên thread). Có thể thêm catch-all.

### Câu hỏi phân tích

1. Interruption là **cooperative** — thread bị interrupt phải **tự check** tại interruption points. So sánh với preemptive cancellation (ví dụ `pthread_cancel`). Trade-off safety vs responsiveness?
2. Nếu thread đang chạy tight loop **không có** `interruption_point()`: interrupt() set flag, nhưng thread **không bao giờ check** → không dừng. Giải pháp?
3. `std::promise` truyền pointer — tại sao không dùng shared variable? (Hint: thread chưa start khi constructor chạy → `thread_local` variable chưa tồn tại.)
4. `thread_interrupted` bị catch bên trong wrapper → thread exit silently. Nếu caller cần biết thread bị interrupt (vs hoàn thành bình thường), interface thay đổi thế nào?

---

## Bài 8 🟡 — `interruptible_wait` cho `std::condition_variable`

### Đặc tả hành vi

Mở rộng `interrupt_flag` (Bài 7) để hỗ trợ interrupt thread đang **block** trên condition variable wait.

**Vấn đề:** Thread đang `cv.wait(lk)` → không chạy → không gọi `interruption_point()` → không thể interrupt. Cần mechanism: khi `interrupt()` được gọi, **notify condition variable** để đánh thức thread.

**Thiết kế:**

Mở rộng `interrupt_flag`:

```
class interrupt_flag {
    std::atomic<bool> flag;
    std::condition_variable* thread_cond;
    std::mutex set_clear_mutex;

public:
    void set();
    bool is_set() const;

    void set_condition_variable(std::condition_variable& cv);
    void clear_condition_variable();

    struct clear_cv_on_destruct { ~clear_cv_on_destruct(); };
};
```

`set()`:
- Set `flag = true`.
- Lock `set_clear_mutex` → nếu `thread_cond != nullptr` → `thread_cond->notify_all()`.

`interruptible_wait(cv, lk)`:
```
void interruptible_wait(std::condition_variable& cv,
                        std::unique_lock<std::mutex>& lk) {
    interruption_point();
    this_thread_interrupt_flag.set_condition_variable(cv);
    interrupt_flag::clear_cv_on_destruct guard;
    interruption_point();
    cv.wait_for(lk, std::chrono::milliseconds(1));
    interruption_point();
}
```

`interruptible_wait(cv, lk, predicate)`:
```
template <typename Predicate>
void interruptible_wait(std::condition_variable& cv,
                        std::unique_lock<std::mutex>& lk,
                        Predicate pred) {
    interruption_point();
    this_thread_interrupt_flag.set_condition_variable(cv);
    interrupt_flag::clear_cv_on_destruct guard;
    while (!this_thread_interrupt_flag.is_set() && !pred()) {
        cv.wait_for(lk, std::chrono::milliseconds(1));
    }
    interruption_point();
}
```

### Ràng buộc cứng

- `set_condition_variable` và `clear_condition_variable` phải protected bằng `set_clear_mutex`.
- `set()` lock `set_clear_mutex` để notify → đảm bảo không notify destroyed cv.
- Dùng `wait_for(..., 1ms)` thay `wait()` — **tại sao**: race giữa `set_condition_variable` và `wait()`. Timeout đảm bảo thread tỉnh dậy định kỳ để check flag, kể cả khi notify bị miss.
- `clear_cv_on_destruct`: RAII guard gọi `clear_condition_variable()` khi scope exit — exception safety.

### Invariant cần giữ

- Thread bị interrupt trong khi wait → tỉnh dậy trong vòng ~1ms → throw `thread_interrupted`.
- `clear_cv_on_destruct` đảm bảo `thread_cond` pointer bị clear **kể cả khi exception** → `set()` từ thread khác không notify destroyed cv.
- `set_clear_mutex` bảo vệ race: thread A gọi `set()` + notify, thread B gọi `clear_condition_variable()`. Mutex đảm bảo không xen kẽ.

### Bài test bắt buộc

**Test 1 — Interrupt blocked thread:** Thread block trên `interruptible_wait(cv, lk, pred)` với pred luôn false. Interrupt từ main thread. Thread phải throw `thread_interrupted` trong vòng 50ms.

**Test 2 — Normal wakeup still works:** Thread chờ trên cv. Thread khác `notify_one()` + set predicate. Thread tỉnh dậy bình thường, **không** throw.

**Test 3 — Interrupt before wait:** Set interrupt flag **trước** khi thread gọi `interruptible_wait`. Thread phải throw ngay (tại `interruption_point()` đầu tiên), **không** enter wait.

**Test 4 — Exception safety:** `set_condition_variable` gọi nhưng exception xảy ra trước `clear_condition_variable` → `clear_cv_on_destruct` phải clear. `set()` sau đó không notify destroyed cv.

### Debug checkpoint

- **Race mà `wait_for` giải quyết:** Thread B gọi `set_condition_variable(cv)` → thread A gọi `set()` + `notify_all(cv)` → thread B chưa vào `cv.wait()` → notification bị miss → thread B block mãi. Với `wait_for(1ms)`: thread B tỉnh dậy sau 1ms, check flag, throw.
- `notify_all` thay `notify_one` trong `set()` — vì ta interrupt **một** thread cụ thể, nhưng cv có thể có nhiều waiters. `notify_all` đảm bảo target thread tỉnh dậy. Các thread khác coi như spurious wakeup.
- `set_clear_mutex`: `set()` lock trước khi read `thread_cond`. `set_condition_variable()` lock trước khi write `thread_cond`. Không lock → data race.

### Câu hỏi phân tích

1. Tại sao không thể dùng `cv.wait(lk)` (không timeout) an toàn? Trace race condition cụ thể: thread B register cv → thread A `set()` + notify → thread B `cv.wait()` → miss → block mãi.
2. `wait_for(1ms)` giới thiệu latency tối đa 1ms cho interruption. Nếu giảm xuống 1μs: CPU usage tăng (spurious wakeup loop). Trade-off?
3. `notify_all` trong `set()`: nếu 100 threads chờ trên cùng cv, tất cả tỉnh dậy. Chỉ 1 thread bị interrupt, 99 threads spurious wakeup. Performance impact?
4. So sánh approach này với `std::stop_token` + `std::condition_variable_any::wait(lock, stop_token, pred)` trong C++20. Approach nào elegant hơn?

---

## Bài 9 🔴 — `interruptible_wait` cho `std::condition_variable_any` (Custom Lock)

### Đặc tả hành vi

Mở rộng `interrupt_flag` (Bài 8) thêm hỗ trợ `std::condition_variable_any` — giải quyết race condition mà `wait_for` timeout chỉ **workaround**, bằng cách dùng **custom lock type** lock **cả** internal mutex lẫn user's lock atomically.

**Insight:** `condition_variable_any` nhận bất kỳ Lockable type — ta tạo custom lock unlock cả hai mutexes atomically khi vào wait, lock cả hai khi ra. Vì `set_clear_mutex` được hold cho đến khi vào `wait()`, interrupt không thể xảy ra giữa register và wait → **no race, no timeout cần**.

### Thiết kế `custom_lock`

Bên trong `interrupt_flag::wait(cv_any, lk)`:

```
struct custom_lock {
    interrupt_flag* self;
    Lockable& lk;

    custom_lock(interrupt_flag* self_, std::condition_variable_any& cv, Lockable& lk_)
        : self(self_), lk(lk_)
    {
        self->set_clear_mutex.lock();          // hold internal mutex
        self->thread_cond_any = &cv;           // register cv
    }

    void unlock() {
        lk.unlock();                           // unlock user's lock
        self->set_clear_mutex.unlock();        // unlock internal mutex
    }

    void lock() {
        std::lock(self->set_clear_mutex, lk);  // lock both, deadlock-free
    }

    ~custom_lock() {
        self->thread_cond_any = nullptr;       // deregister cv
        self->set_clear_mutex.unlock();        // release internal mutex
    }
};
```

Flow:
1. `custom_lock` constructor: lock `set_clear_mutex`, register cv.
2. `interruption_point()`: nếu flag set → throw (trước `wait`). `set_clear_mutex` đang held → `set()` từ thread khác block tại `set_clear_mutex` → không notify destroyed cv.
3. `cv.wait(custom_lock)`: cv gọi `custom_lock::unlock()` → unlock cả `lk` lẫn `set_clear_mutex`. Bây giờ `set()` có thể acquire `set_clear_mutex` → notify cv.
4. cv wakeup: cv gọi `custom_lock::lock()` → lock cả hai lại.
5. `interruption_point()`: check flag lần nữa.
6. `custom_lock` destructor: clear cv pointer, unlock `set_clear_mutex`.

### Ràng buộc cứng

- `interrupt_flag` phải track cả `std::condition_variable* thread_cond` lẫn `std::condition_variable_any* thread_cond_any`.
- `set()` phải check cả hai pointers: notify appropriate cv.
- `custom_lock` phải satisfy Lockable concept: có `lock()`, `unlock()`, `try_lock()` (optional).
- `std::lock(mutex1, mutex2)` dùng deadlock avoidance algorithm.

### Bài test bắt buộc

**Test 1 — Interrupt `cv_any` wait:** Thread block trên `interruptible_wait(cv_any, lk)` với custom mutex type. Interrupt → throw `thread_interrupted`. **Không timeout** — interrupt phải precise.

**Test 2 — No race:** Stress test: 100 iterations. Thread A liên tục vào/ra `interruptible_wait`. Thread B liên tục gọi `interrupt()`. Không crash, không hang, không data race.

**Test 3 — Regular `condition_variable` vẫn hoạt động:** `interruptible_wait(cv, lk)` (bài 8) vẫn dùng timeout approach. Hai mechanisms cùng tồn tại.

### Debug checkpoint

- `custom_lock` constructor lock `set_clear_mutex` → destructor unlock. Nếu exception giữa constructor và destructor (ví dụ `interruption_point()` throw): destructor vẫn chạy (stack unwinding) → unlock `set_clear_mutex`. OK.
- `custom_lock::lock()` dùng `std::lock(a, b)` — deadlock-free. Nếu dùng `a.lock(); b.lock();` → deadlock nếu `set()` lock `set_clear_mutex` rồi chờ `lk`.
- `cv.wait(custom_lock)` gọi `unlock()` **trước** khi sleep, `lock()` **sau** khi wake. Nếu `custom_lock::unlock()` chỉ unlock `lk` mà không unlock `set_clear_mutex` → `set()` block mãi → interrupt không bao giờ đến.

### Câu hỏi phân tích

1. So sánh `condition_variable` approach (timeout-based, Bài 8) với `condition_variable_any` approach (custom lock, Bài 9). Cái nào precise hơn? Cái nào đơn giản hơn?
2. `custom_lock` đóng vai trò "bridge" giữa user's lock và internal mutex. Giải thích tại sao `unlock()` phải unlock **cả hai**, và `lock()` phải lock **cả hai**.
3. Nếu user's `Lockable` type có side effects trong `lock()`/`unlock()` (ví dụ logging), `custom_lock` có gây unexpected behavior không?
4. `std::lock(a, b)` trong `custom_lock::lock()` — giải thích deadlock avoidance algorithm: try lock a, try lock b, nếu fail → unlock a, try lại ngược thứ tự.

---

## Bài 10 🟡 — `interruptible_wait` cho `std::future`

### Đặc tả hành vi

Viết `interruptible_wait` cho `std::future` — chờ future ready trong khi vẫn có thể bị interrupt.

```
template <typename T>
void interruptible_wait(std::future<T>& f);
```

Hành vi:
- Loop: check interrupt flag → `f.wait_for(1ms)` → check ready → repeat.
- Nếu interrupt flag set → throw `thread_interrupted`.
- Nếu future ready → return bình thường.

### Ràng buộc cứng

- Dùng `wait_for(1ms)` — không có cách nào interruptible wait trên future mà không timeout (future không có notify mechanism bên ngoài).
- Check interrupt flag **trước** wait_for mỗi iteration.
- Check `future_status::ready` sau wait_for → break.
- Cuối cùng gọi `interruption_point()` để throw nếu interrupted.

### Bài test bắt buộc

**Test 1 — Future ready:** `std::promise<int> p; auto f = p.get_future(); p.set_value(42);` `interruptible_wait(f);` → return ngay, `f.get() == 42`.

**Test 2 — Interrupt before ready:** `std::promise<int> p; auto f = p.get_future();` Thread A: `interruptible_wait(f);` Thread B: `sleep(50ms); thread_A.interrupt();` Thread A throw `thread_interrupted`.

**Test 3 — Latency:** Đo thời gian từ `interrupt()` đến `thread_interrupted` throw. Phải < 5ms (vài iterations của 1ms timeout).

### Câu hỏi phân tích

1. Tại sao không thể interrupt `future::wait()` trực tiếp? (Hint: future chỉ wake khi promise set — không có external notification mechanism.)
2. `wait_for(1ms)` → thread tỉnh dậy ~1000 lần/giây khi future chưa ready. CPU overhead?
3. So sánh: interrupt future wait vs interrupt cv wait (Bài 8). Cái nào dùng timeout, cái nào có thể precise? Tại sao khác nhau?
4. C++20 `std::jthread` + `std::stop_token`: `stop_token` có thể integrate vào `condition_variable_any::wait` nhưng **không** vào `future::wait`. Tại sao limitation này tồn tại?

---

# PHẦN C — Integration

---

## Bài 11 🔴 — Parallel Quicksort dùng Work-Stealing Thread Pool

### Đặc tả hành vi

Viết `parallel_quick_sort<T>` dùng `work_stealing_thread_pool` (Bài 6) — synthesis của toàn bộ chapter.

```
template <typename T>
std::list<T> parallel_quick_sort(std::list<T> input);
```

### Thiết kế nội bộ

```
template <typename T>
struct sorter {
    work_stealing_thread_pool pool;

    std::list<T> do_sort(std::list<T>& chunk_data) {
        if (chunk_data.empty()) return chunk_data;

        std::list<T> result;
        result.splice(result.begin(), chunk_data, chunk_data.begin());
        T const& pivot = *result.begin();

        auto divide_point = std::partition(
            chunk_data.begin(), chunk_data.end(),
            [&](T const& val) { return val < pivot; });

        std::list<T> new_lower;
        new_lower.splice(new_lower.end(), chunk_data,
                         chunk_data.begin(), divide_point);

        std::future<std::list<T>> new_lower_future =
            pool.submit(std::bind(&sorter::do_sort, this,
                                  std::move(new_lower)));

        std::list<T> new_higher(do_sort(chunk_data));

        result.splice(result.end(), new_higher);

        while (new_lower_future.wait_for(0s) == std::future_status::timeout)
            pool.run_pending_task();    // ← work while waiting

        result.splice(result.begin(), new_lower_future.get());
        return result;
    }
};
```

### Ràng buộc cứng

- Dùng `work_stealing_thread_pool` (Bài 6).
- Lower chunk submit vào pool → future. Upper chunk sort trực tiếp (recursive).
- While chờ lower: gọi `pool.run_pending_task()` → phá deadlock + tận dụng CPU.
- `std::bind` hoặc lambda để submit `do_sort` member function.
- Input: `std::list<T>` (không phải vector — cần `splice` O(1)).

### Invariant cần giữ

- Output sorted ascending.
- Stable relative order **không đảm bảo** (quicksort unstable).
- Không deadlock: `run_pending_task()` đảm bảo thread chờ vẫn productive.
- Tất cả elements từ input xuất hiện đúng một lần trong output.

### Bài test bắt buộc

**Test 1 — Correctness small:** `{5, 3, 1, 4, 2}` → `{1, 2, 3, 4, 5}`.

**Test 2 — Correctness large:** 500,000 random ints. `std::is_sorted(result)`.

**Test 3 — Performance:** So sánh thời gian: (a) sequential `std::sort`, (b) `parallel_quick_sort` với simple pool (Bài 4), (c) `parallel_quick_sort` với work-stealing pool (Bài 6). Work-stealing phải nhanh nhất hoặc comparable.

**Test 4 — Already sorted:** Input đã sorted → quicksort worst case (pivot luôn min/max). Vẫn đúng, không stack overflow (pool giới hạn thread count).

**Test 5 — All equal:** `{5,5,5,5,5}` → `{5,5,5,5,5}`. Partition edge case.

### Debug checkpoint

- `std::bind(&sorter::do_sort, this, std::move(new_lower))`: `std::bind` copy arguments mặc định! `std::move(new_lower)` tạo rvalue, `bind` **move** nó vào internal storage. Khi bind object được invoke, argument được **copy** từ internal storage tới function parameter. → `do_sort` nhận `std::list<T>` **by value** (move từ bind storage). Nếu `do_sort` nhận by reference → dangling.
- Alternative: lambda `[this, lower = std::move(new_lower)]() mutable { return do_sort(lower); }`. Rõ ràng hơn `std::bind`.
- `pool.run_pending_task()` trong wait loop: nếu task trong queue throw → exception propagate ra wait loop → `new_lower_future` destructor wait → OK. Nhưng sort result bị corrupt. Cần catch + handle.
- `wait_for(0s)` vs `wait_for(0ms)`: cả hai equivalent — zero duration → check ready.

### Câu hỏi phân tích

1. Trace execution: input 8 elements. Quicksort tạo tree of tasks. Vẽ task tree. Pool 4 threads. Đánh dấu task nào chạy trên thread nào. Work stealing xảy ra ở đâu?
2. `run_pending_task()` trong wait loop: thread X chờ task A, chạy task B thay. Task B chờ task C, X chạy task C. Stack depth: X → wait(A) → run(B) → wait(C) → run(C). Nếu recursion sâu → stack overflow. Giải pháp thực tế?
3. Quicksort submit **lower** chunk, sort **upper** trực tiếp. Nếu đảo (submit upper, sort lower trực tiếp) — performance thay đổi? (Hint: partition thường tạo lower nhỏ hơn upper khi pivot = first element.)
4. So sánh 3 approaches: (a) `std::async` (Chapter 4), (b) manual thread pool (Chapter 8, Listing 8.1), (c) work-stealing pool (Chapter 9, bài này). Complexity, performance, scalability cho mỗi approach.

---

# Thứ tự làm đề xuất

```
Bài 1  (function_wrapper — foundation cho tất cả pool versions)
    ↓
Bài 2  (simple_thread_pool — fire-and-forget)
    ↓
Bài 3  (waitable_thread_pool — submit returns future)
    ↓
Bài 4  (run_pending_task — tasks wait for tasks)
    ↓
Bài 7  (interrupt_flag + interruptible_thread — new topic)
    ↓
Bài 8  (interruptible_wait for cv — extends Bài 7)
    ↓
Bài 10 (interruptible_wait for future — simpler than Bài 9)
    ↓
Bài 5  (thread-local queues — extends Bài 4)
    ↓
Bài 6  (work stealing — extends Bài 5, hardest pool)
    ↓
Bài 9  (interruptible_wait for cv_any — hardest interruption)
    ↓
Bài 11 (parallel quicksort — synthesis of everything)
```

---

# Checklist "done" cho mỗi bài

- [ ] Compile sạch với `-Wall -Wextra -Wpedantic`
- [ ] Chạy sạch với `-fsanitize=thread` (không data race report)
- [ ] Chạy sạch với `-fsanitize=address` (không memory error)
- [ ] Tất cả test cases pass, bao gồm exception paths
- [ ] Benchmark: pool version nhanh hơn sequential cho workload đủ lớn
- [ ] Có thể trả lời tất cả câu hỏi phân tích bằng lời, không nhìn code
- [ ] Có thể trace task flow qua pool: submit → queue → worker → execute → future ready
- [ ] Giải thích được tại sao `function_wrapper` cần thiết thay `std::function`
- [ ] Giải thích được work stealing: LIFO owner, FIFO thief, round-robin search
- [ ] Giải thích được interruption mechanism: cooperative, interrupt_flag, interruptible_wait
