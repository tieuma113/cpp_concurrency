# Bộ bài tập chuyên sâu — C++ Concurrency in Action, Chapter 6 (Đợt 2)

> **Đối tượng**: lock-based concurrent data structures (stack, queue with fine-grained locking, hash-bucketed lookup table, fine-node-locked linked list).
> **Mục tiêu**: kéo từ L3 (Apply) lên L4–L5 (Analyze/Evaluate). Tránh các góc đã drill ở đợt trước (move ops self-assignment, interface race của `top()/pop()`).
> **Toolchain**: `g++ -std=c++17 -pthread -g -fsanitize=thread`.

---

## (0) Foundational warm-up — vẽ và liệt kê

Không phải định nghĩa. Đây là bài kiểm tra "trong đầu có hình không".

### W1. ASCII diagram queue dummy node (listing 6.6)

Vẽ trạng thái queue tại 4 thời điểm sau:
1. Sau constructor (queue rỗng).
2. Sau `push(A)`.
3. Sau `push(B)`.
4. Sau `try_pop()` (kết quả: `A`).

Mỗi node phải thể hiện rõ: ô `data` (con trỏ), ô `next` (con trỏ), và `head`/`tail` đang trỏ vào đâu. Đặc biệt: chỉ ra ô `data` của dummy node là gì ở mỗi bước.

### W2. Liệt kê đúng 5 invariant của `threadsafe_queue` (listing 6.7–6.10)

Yêu cầu: mỗi invariant phải nói được "luôn đúng" tại điểm nào trong code (đầu/cuối hàm, hay xuyên suốt). Tối đa 1 dòng mỗi invariant.

Ví dụ của một invariant đúng dạng:
> *"Tại mọi thời điểm có lock `head_mutex` được giải phóng, `head` trỏ tới một node hợp lệ (không null) thuộc danh sách hiện tại."*

### W3. Bảng 4 cột — primitive nào, ở đâu, vì sao

| Listing  | Primitive       | Bảo vệ cái gì                    | Vì sao chọn primitive đó (1 câu) |
|---------:|-----------------|----------------------------------|----------------------------------|
| 6.1 stack| `std::mutex`    |                                  |                                  |
| 6.2 queue| `mutex` + `cv`  |                                  |                                  |
| 6.6 queue| 2× `mutex`      |                                  |                                  |
| 6.11 map | `shared_mutex`  |                                  |                                  |
| 6.13 list| `mutex` per node|                                  |                                  |

---

## (1) Câu hỏi tự luận chuyên sâu

Format chuẩn: **Context → State/Interleaving → Primitive → Invariant → Trade-off → Edge case**.

### E1 🔴 — `get_tail()` phải gọi BÊN TRONG `head_mutex` lock

Trong listing 6.6, hàm `pop_head()` lock `head_mutex` rồi mới gọi `get_tail()` (hàm này tự lock `tail_mutex`). Có vẻ thừa: tại sao không gọi `get_tail()` trước khi lock `head_mutex` để giảm critical section?

Yêu cầu:
- (a) Dựng kịch bản cụ thể với **3 threads** (T1 đang pop, T2 và T3 đang push) chứng minh nếu đảo thứ tự (gọi `get_tail()` ngoài lock), `head` có thể bị di chuyển vượt qua node mà `get_tail()` trả về, dẫn đến destruction một node "vô chủ".
- (b) Vẽ ASCII linked list trước/sau interleaving để minh họa.
- (c) Nêu chính xác invariant nào bị break, và vì sao thứ tự lock đúng (head trước, tail sau khi đã có head_lock) **giữ** invariant đó.
- (d) Hệ quả lock ordering này có rủi ro deadlock không khi push() lock theo chiều ngược (tail trước)? Giải thích vì sao không.

### E2 🔴 — Exception safety của `push()` trong listing 6.8

Liệt kê **theo thứ tự thực thi** mọi điểm có thể throw exception. Với mỗi điểm:
- Loại exception nào (allocation, copy ctor, move ctor, lock acquisition…).
- Trạng thái queue ngay tại điểm đó (đã thay đổi member chưa? lock đã giữ chưa?).
- Cleanup tự động bởi cái gì (RAII, smart pointer destructor…).
- Guarantee đạt được (basic / strong / nothrow).

Sau đó: chứng minh `push()` đạt **strong exception guarantee** (queue trông như chưa từng được gọi). Lưu ý đặc biệt: `tail->data = new_data;` có thể throw không? Điểm mấu chốt nằm ở chỗ nào?

### E3 🟡 — `notify_one()` đặt trong hay ngoài lock

Listing 6.8 gọi `data_cond.notify_one()` **sau khi** `tail_mutex` được giải phóng (lock_guard ra khỏi scope nội). Một biến thể "naive" giữ lock khi notify:
```cpp
void push(T new_value) {
    auto new_data = std::make_shared<T>(std::move(new_value));
    std::unique_ptr<node> p(new node);
    std::lock_guard<std::mutex> lk(tail_mutex);
    tail->data = new_data;
    tail->next = std::move(p);
    tail = tail->next.get();
    data_cond.notify_one();   // <-- vẫn trong lock
}
```

Yêu cầu:
- (a) Vẽ ASCII timeline 2 threads (Producer P, Consumer C đang chờ trong `wait_and_pop`) cho **cả hai** phiên bản. Thể hiện rõ trạng thái lock của P và trạng thái wait/wake của C tại từng tick.
- (b) Phiên bản nào tốt hơn về throughput, và vì sao? Trade-off cụ thể là gì?
- (c) **Twist**: notify_one trong `threadsafe_queue` này thực ra dùng `head_mutex` ở phía consumer chứ không phải `tail_mutex`. Vậy việc giữ `tail_mutex` khi notify thực sự gây hại không? Phân tích lại.

### E4 🔴 — Tại sao `num_buckets` cố định lúc khởi tạo (listing 6.11)

`std::unordered_map` có rehash để giữ load factor thấp. Listing 6.11 thì không. Yêu cầu phân tích:
- (a) Tại sao mặc định 19? Nói chính xác lợi ích của số nguyên tố với hash modulo.
- (b) Nếu insert 1 triệu key duy nhất vào bảng 19 bucket, `value_for(key)` worst-case là O(?). So với `std::unordered_map`?
- (c) Vì sao không thể dễ dàng thêm rehash? Cụ thể: nếu thread T1 đang giữ `bucket[5].mutex` đọc, thread T2 trigger rehash → toàn bộ buckets bị tái phân phối → `bucket[5]` không còn tồn tại ở vị trí cũ. Liệt kê 3 vấn đề cụ thể (lifetime, lock identity, iterator/reference invalidation).
- (d) Thiết kế thay thế: nếu PHẢI hỗ trợ rehash, bạn dùng cơ chế gì? Gợi ý các từ khoá: epoch-based, RCU-style, generation counter, double-buffered table. Chọn 1 và phác sơ đồ.

### E5 🔴 — `get_map()` (listing 6.12): snapshot có "atomic" không?

Code lock TẤT CẢ bucket mutexes (`unique_lock`) rồi mới copy.

- (a) Đây là atomic snapshot theo nghĩa nào? Định nghĩa "atomic snapshot" cho lookup table.
- (b) Trong khi `get_map()` chạy, một thread call `value_for(key)` block bao lâu? Worst case so với best case?
- (c) **Phương án B**: lock bucket 1 → copy → unlock; lock bucket 2 → copy → unlock; … (lock từng bucket ngắn hạn). Đặt câu hỏi: kết quả `std::map` trả về có còn "đúng" không? "Đúng" theo nghĩa nào? Có bị mất key không? Có chứa key đã bị remove không? Có vi phạm bất kỳ invariant cấu trúc nào không?
- (d) Trong production (lookup table chứa millions of entries, `get_map()` được gọi cho diagnostic dump), bạn sẽ chọn A hay B? Lý do.

### E6 🔴 — Hand-over-hand list (listing 6.13): có thể có 2 iteration concurrent không?

Sách viết: *"the mutex for each node must be locked in turn, the threads can't pass each other"*. Phân tích cụ thể:

- (a) Thread A và Thread B cùng gọi `for_each(f)` — tức cùng chiều, đầu danh sách trở đi. Vẽ ASCII timeline node-by-node. Có deadlock không? Có concurrency không hay full-serial?
- (b) Thread A đang `for_each` (đi từ đầu), thread B đang... cũng chỉ đi được từ đầu (list không có double-link). Vậy B có thể "đi ngược chiều" theo nghĩa nào? Cấu trúc có loại trừ deadlock không? Lý do.
- (c) Thread A `remove_if(pred)` đang giữ lock(node_3) muốn lock(node_4). Thread B `for_each` đang giữ lock(node_3)? Không thể — vì sao? Mô tả handover protocol chính xác (lock current, lock next, release prev — thứ tự đầy đủ).
- (d) **Edge case**: thread A xoá node_4 trong khi thread B đang giữ lock(node_4). Có happen được không? Chứng minh không, dựa trên handover protocol.
- (e) Hệ quả thực tế: nếu một `for_each` callback chạy 10ms ở node_5, mọi thread khác phải đợi tối thiểu bao lâu để **đi qua** node_5? Đây có phải fine-grained locking thật sự không?

---

## (2) Câu hỏi cross-chapter

### C1 🔴 — Memory ordering (chap 5) trong queue listing 6.6

Một junior developer hỏi:
> *"`tail` là `node*` raw pointer, được đọc/ghi từ nhiều threads. Sao không phải `std::atomic<node*>`? Như vậy có data race không?"*

Yêu cầu trả lời như interview:
- (a) Định nghĩa **data race** chính xác (theo C++ memory model: hai access đến cùng location, ít nhất một là write, không có happens-before).
- (b) Mutex lock/unlock cung cấp synchronization gì? Cụ thể: `lock()` đóng vai trò gì về memory ordering? `unlock()` đóng vai trò gì? Tham chiếu acquire/release.
- (c) Áp dụng vào listing 6.6: chứng minh KHÔNG có data race trên `tail` mặc dù là raw pointer. Vẽ happens-before edges giữa hai threads (P push, C try_pop).
- (d) Điều kiện gì sẽ phá vỡ chứng minh trên? (Gợi ý: nếu một function nào đó đọc `tail` mà không lock `tail_mutex`?)

### C2 🟡 — Lock ordering deadlock (chap 3) trong queue 2-mutex

- (a) Trong nội bộ `threadsafe_queue` listing 6.6, có bao giờ giữ đồng thời 2 mutex (head + tail)? Chỉ ra hàm cụ thể.
- (b) Lock order trong hàm đó là gì? Nếu user code trực tiếp lock 2 mutex theo chiều ngược lại — họ có cách nào không? (Hint: 2 mutex là `private`.)
- (c) `std::lock(m1, m2)` từ chap 3 có cần thiết ở đây không? Vì sao?
- (d) Giả sử ai đó thêm hàm `transfer(other_queue&)` di chuyển N items qua queue khác. Phân tích deadlock risk và đề xuất cách viết đúng.

### C3 🔴 — Condition variable predicate (chap 4) gọi function tự lock

`wait_for_data()` (listing 6.9) wait với predicate `head.get() != get_tail()`. `get_tail()` tự lock `tail_mutex`.

- (a) `cv.wait(lk, pred)` — pred được gọi khi nào? Liệt kê đầy đủ các thời điểm (initial entry, sau spurious wakeup, sau notify_one).
- (b) Tại mỗi lần gọi pred, lock state của `head_mutex` là gì? Của `tail_mutex` là gì?
- (c) Nguy cơ deadlock: thread T1 đang trong `wait()` (giữ `head_mutex` trong lúc check pred, đang lock `tail_mutex` cho `get_tail()`). Thread T2 muốn lock cả hai theo chiều khác — có không? Trace cụ thể.
- (d) Performance: pred gọi `get_tail()` mỗi lần wakeup → mỗi lần đều phải lock/unlock `tail_mutex`. Có ảnh hưởng không? Có cách nào "cache" lại không (và lý do tại sao không nên)?

### C4 🟡 — Stack listing 6.1 vs queue listing 6.6: vì sao không phân tách 2 mutex cho stack?

Stack chỉ dùng 1 mutex. Tại sao không áp dụng "fine-grained" cho stack?

- (a) Stack có dummy-node trick được không? Nếu được, dummy ở đâu — đỉnh hay đáy? Phân tích.
- (b) Nếu cố tách: top_mutex và bottom_mutex — bottom có ai access không? Hệ quả về concurrency là gì?
- (c) Kết luận về cấu trúc nào "bẩm sinh" friendlier với fine-grained locking, và tại sao queue thắng stack ở đây.

---

## (3) Skeleton + intentional bugs

> **Format**: code có chú thích `// SUSPICIOUS` ở những vùng nghi vấn (số bug được công bố ở đầu mỗi bài). Học viên: chạy thử với TSan, mô tả interleaving cụ thể gây sai, sửa, trả lời analysis questions.
>
> Compile: `g++ -std=c++17 -pthread -g -fsanitize=thread -O1 file.cpp -o test`

### B1 🟡 — Stack với check-then-act gãy (1 bug)

```cpp
template<typename T>
class threadsafe_stack {
    mutable std::mutex m;
    std::stack<T> data;
public:
    bool empty() const {
        std::lock_guard<std::mutex> lk(m);
        return data.empty();
    }

    // SUSPICIOUS
    std::shared_ptr<T> pop() {
        if (empty()) {
            return nullptr;
        }
        std::lock_guard<std::mutex> lk(m);
        auto res = std::make_shared<T>(std::move(data.top()));
        data.pop();
        return res;
    }

    void push(T v) {
        std::lock_guard<std::mutex> lk(m);
        data.push(std::move(v));
    }
};
```

Câu hỏi:
- Bug ở đâu? Đặt tên (interface race / TOCTOU / lock-not-held / …).
- Dựng interleaving cụ thể (2 threads, 1 element trong stack) khiến `pop()` crash hoặc UB.
- Sửa thế nào để KHÔNG mất concurrency với `push()`? (Gợi ý: chap 3 đã cover, nhưng fix lần này phải nói rõ tại sao trả `nullptr` vs throw `empty_stack` là 2 lựa chọn API.)

### B2 🔴 — Fine-grained queue (2 bugs)

```cpp
template<typename T>
class threadsafe_queue {
    struct node {
        std::shared_ptr<T> data;
        std::unique_ptr<node> next;
    };
    std::mutex head_mutex, tail_mutex;
    std::unique_ptr<node> head;
    node* tail;

    node* get_tail() {
        std::lock_guard<std::mutex> lk(tail_mutex);
        return tail;
    }

    // SUSPICIOUS
    std::unique_ptr<node> pop_head() {
        node* const old_tail = get_tail();
        std::lock_guard<std::mutex> lk(head_mutex);
        if (head.get() == old_tail) return nullptr;
        auto old_head = std::move(head);
        head = std::move(old_head->next);
        return old_head;
    }

public:
    threadsafe_queue() : head(new node), tail(head.get()) {}

    std::shared_ptr<T> try_pop() {
        auto old_head = pop_head();
        return old_head ? old_head->data : nullptr;
    }

    // SUSPICIOUS
    void push(T new_value) {
        auto new_data = std::make_shared<T>(std::move(new_value));
        std::unique_ptr<node> p(new node);
        std::lock_guard<std::mutex> lk(tail_mutex);
        node* const new_tail = p.get();
        tail->next = std::move(p);  // <-- thứ tự?
        tail->data = new_data;
        tail = new_tail;
    }
};
```

Câu hỏi:
- Bug 1 (`pop_head`): mô tả kịch bản 3 threads (mỗi thread label hành động trên đường đi). Trạng thái sau interleaving là gì? Invariant nào bị vỡ?
- Bug 2 (`push`): đảo `tail->data = new_data` xuống dưới `tail->next = std::move(p)`. Vấn đề là gì? **Hint**: thread consumer thấy được node mới ngay khi `next` được gán, nhưng `data` chưa kịp set. Vẽ trace cụ thể.
- Sửa từng bug và **chạy lại với TSan** để xác nhận không còn warning. Câu hỏi: TSan có bắt được CẢ HAI bug không? Bug nào TSan có thể miss và vì sao?

### B3 🔴 — Hash bucket với primitive sai (3 bugs)

```cpp
class bucket_type {
    using bucket_value = std::pair<Key, Value>;
    std::list<bucket_value> data;
    mutable std::shared_mutex mutex;

    auto find_entry_for(Key const& k) const {
        return std::find_if(data.begin(), data.end(),
            [&](auto const& e){ return e.first == k; });
    }

public:
    // SUSPICIOUS
    Value value_for(Key const& k, Value const& def) const {
        std::unique_lock<std::shared_mutex> lk(mutex);
        auto it = find_entry_for(k);
        return it == data.end() ? def : it->second;
    }

    // SUSPICIOUS
    void add_or_update(Key const& k, Value const& v) {
        std::shared_lock<std::shared_mutex> lk(mutex);
        auto it = find_entry_for(k);
        if (it == data.end()) data.push_back({k, v});
        else it->second = v;
    }

    // SUSPICIOUS
    void remove(Key const& k) {
        auto it = find_entry_for(k);
        std::unique_lock<std::shared_mutex> lk(mutex);
        if (it != data.end()) data.erase(it);
    }
};
```

Câu hỏi:
- Bug 1 (`value_for`): tại sao `unique_lock` ở read path là tệ về performance NHƯNG đúng về correctness? (Trick câu hỏi.)
- Bug 2 (`add_or_update`): `shared_lock` cho write path là **sai**. Thiệt hại cụ thể là gì? `std::list` modification trong khi reader khác đang iterate gây UB như thế nào?
- Bug 3 (`remove`): TOCTOU — `find_entry_for` chạy ngoài lock, lock acquire muộn. Iterator có thể đã invalid. Mô tả interleaving với 2 threads cùng `remove(k)`.
- Sửa cả 3, sau đó chạy stress test 4 threads × 1M ops mỗi thread.

---

## (4) From-scratch — không skeleton

### S1 🟡 — Bounded queue dựa trên listing 6.6

Yêu cầu:
- Constructor nhận `size_t capacity`. `push()` block khi đầy, `try_push()` trả `false` ngay.
- Vẫn giữ tách `head_mutex` / `tail_mutex`.
- Hai condition variable: `not_full`, `not_empty`. Mỗi cv gắn với mutex nào?
- Counter `current_size` lưu ở đâu? Bảo vệ bằng gì? Có cần `std::atomic`?

Câu hỏi phân tích sau khi viết:
- Có thể dùng 1 cv duy nhất không? Tại sao 2 hợp lý hơn?
- `notify_one` của `not_full` được gọi ở chỗ nào trong `try_pop`?
- Edge case: capacity = 0. Code có đúng không? Có ý nghĩa không?
- Edge case: capacity = 1. So với 1 mutex bao quát thì có lợi gì không?

### S2 🔴 — Concurrent priority queue

Yêu cầu:
- API: `push(T, priority)`, `wait_and_pop()` (lấy phần tử priority cao nhất).
- Internal: `std::priority_queue<...>` được phép.

Câu hỏi PHẢI trả lời trong comment đầu file:
- Vì sao priority queue **không thể** fine-grained như queue thường?
- Trade-off so với 2 single-end queues: có giải pháp nào tốt hơn `std::priority_queue` + 1 mutex cho workload write-heavy?
- Bài này thực sự là về **nhận ra giới hạn** của lock-based fine-grained design. Nói rõ lý do trong commentary.

### S3 🔴 — `get_or_compute(key, factory)` cho lookup table

Mở rộng listing 6.11 thêm hàm:
```cpp
Value get_or_compute(Key const& k, std::function<Value()> factory);
```
Hợp đồng:
- Nếu key tồn tại → trả value hiện tại.
- Nếu không → gọi `factory()` đúng 1 lần (kể cả khi 100 threads cùng yêu cầu cùng key đó), lưu, trả.
- `factory()` có thể **chậm** (vài giây — ví dụ DNS lookup, file read). KHÔNG được giữ lock toàn bucket trong lúc factory chạy.

Hint:
- Naive: lock unique → check → factory() → insert → unlock. Vấn đề: 100 threads bị serialize hoàn toàn ở bucket đó.
- Tốt hơn: per-key promise/future hoặc per-key `call_once` flag. Cấu trúc cụ thể thế nào?
- Edge: factory throw exception → state là gì? Threads khác đang chờ thấy gì?

Câu hỏi sau khi viết:
- Liệt kê **3 race condition** có thể xảy ra trong implementation đầu tiên của bạn và cách fix.
- So sánh với `folly::Singleton` hay `absl::call_once` (chỉ cần kể tên kỹ thuật).

---

## (5) Checklist — Safety / Liveness / Performance

Áp dụng cho mọi cấu trúc lock-based bạn viết hoặc review.

### Safety (correctness)

- [ ] **S1**. Mỗi shared mutable data được bảo vệ bởi đúng 1 mutex (hoặc tập mutex xác định). Liệt kê được mapping data → mutex.
- [ ] **S2**. Không có raw pointer/reference vào internal data thoát ra ngoài qua return value (trừ khi có ownership transfer rõ ràng — `shared_ptr`/`unique_ptr`).
- [ ] **S3**. Không có check-then-act mà giữa check và act mutex bị release.
- [ ] **S4**. User-supplied callbacks (như `for_each(f)`) được gọi với mutex giữ — đã hiểu deadlock risk và đã document.
- [ ] **S5**. Exception safety: với mỗi member function viết được phân tích "throw point → state → guarantee".
- [ ] **S6**. Move/copy operations không leak invariant nội bộ (ví dụ: copy ctor lock source mutex trước khi đọc).

### Liveness (no deadlock / no starvation)

- [ ] **L1**. Lock ordering nhất quán nếu có >1 mutex giữ đồng thời. Document order.
- [ ] **L2**. Không lock được giữ qua I/O / sleep / call ngoài tầm kiểm soát.
- [ ] **L3**. `notify_one` đủ — hoặc bắt buộc dùng `notify_all` (lý do). Predicate đảm bảo no missed wakeup.
- [ ] **L4**. `shared_mutex` có chính sách tránh writer starvation hợp lý (implementation defined — biết platform).
- [ ] **L5**. Nếu có blocking `wait_*`, có cơ chế cancel/timeout (hoặc đã chấp nhận hệ quả).

### Performance

- [ ] **P1**. Critical section nhỏ nhất có thể. Memory allocation/deallocation NẰM NGOÀI lock.
- [ ] **P2**. Read path dùng `shared_lock` khi workload read-heavy. Đo trước khi tin.
- [ ] **P3**. False sharing giữa các mutex/atomic được kiểm tra (alignment 64B / `alignas(std::hardware_destructive_interference_size)`).
- [ ] **P4**. Số mutex tỉ lệ với contention thực — không over-engineer (1 mutex/node hợp lý cho list lớn, vô lý cho list 5 phần tử).
- [ ] **P5**. Notification không lãng phí: `notify_one` thay vì `notify_all` nếu chỉ 1 waiter cần wake.
- [ ] **P6**. Fast path không lock nếu được (ví dụ: snapshot count với `atomic<size_t>` cho `empty()`).

---

## (6) Short oral — interview rapid fire

Trả lời mỗi câu **≤ 30 giây**, English keyword summary cho phần bold.

1. **Why dummy node** in fine-grained queue?
2. Vì sao `tail` là `node*` raw, còn `head` là `unique_ptr<node>`?
3. **Why prime number of buckets** in hash table?
4. `std::shared_mutex` writer starvation — concrete scenario?
5. Vì sao stack chỉ cần 1 mutex còn queue tách được 2?
6. **Hand-over-hand locking** — định nghĩa trong 1 câu?
7. `notify_one` vs `notify_all` — chọn sai gây hại gì?
8. Vì sao predicate trong `cv.wait(lk, pred)` phải idempotent (gọi nhiều lần OK)?
9. Exception trong `push()` listing 6.8 → strong guarantee đến từ đâu?
10. **Per-bucket lock** vs single lock — break-even point khi nào (qualitative)?
11. Vì sao thread-safe map listing 6.11 KHÔNG cung cấp iterator?
12. `get_map()` snapshot consistent — atomic theo nghĩa nào?
13. Trong list listing 6.13, `remove_if` xoá node trong khi iterator khác đang trỏ vào nó — có happen được không, và vì sao?
14. **Coarse-grained vs fine-grained** locking — 1 câu trade-off.
15. Lock-based queue có cần `std::atomic` member nào không? Vì sao không?

---

## (7) Mock interview — 45 phút

### Scenario

> Bạn đang phỏng vấn vị trí Senior Systems Engineer tại một công ty ad-tech. Interviewer:
>
> *"We run a real-time bidding service. Every ad impression triggers a lookup against an in-memory feature store: given a `user_id`, return that user's feature vector (~1 KB). The store has ~50M entries, fits in RAM. Read:write ratio is 95:5. Updates come from a stream consumer. Throughput target per node: 200k QPS. Latency p99: <1ms. Single-process, multi-threaded C++ service.*
>
> *Walk me through the data structure and locking strategy. I'll interrupt with follow-ups."*

Tự trả lời, ghi audio hoặc viết, theo các sub-questions interviewer SẼ đặt ra dưới đây (đừng đọc trước — trả lời từng cái khi đến):

**Round 1 — Data structure choice (10 phút)**
- Q1.1: Bạn chọn cấu trúc gì? Vì sao không phải `std::unordered_map` + 1 `shared_mutex`?
- Q1.2: Bao nhiêu bucket? Cố định hay dynamic? Lý do?
- Q1.3: 50M entries, 19 bucket → bucket size ~2.6M. Tệ không? Bao nhiêu bucket là hợp lý? (Họ muốn nghe tính toán, không phải số tròn.)
- Q1.4: Vì sao không dùng concurrent hashmap có sẵn (TBB, folly)?

**Round 2 — Locking & concurrency (15 phút)**
- Q2.1: Per-bucket `std::shared_mutex`. Read 200k QPS × 95% = 190k read/s. Write 10k/s. Có concurrency thực sự không?
- Q2.2: Nếu một bucket nóng (1 user_id phổ biến), bucket lock có thành bottleneck không? Cách giảm?
- Q2.3: Update path: feature vector 1KB. Copy vào trong lock hay swap pointer? Trade-off?
- Q2.4: **Curveball**: nếu update phải atomic với 2-3 keys liên quan (consistency), bạn handle thế nào? Lock multiple bucket — order ra sao?

**Round 3 — Edge & failure (10 phút)**
- Q3.1: Một thread update cầm lock 50ms vì allocate gây page fault. Hậu quả? Mitigation?
- Q3.2: Memory budget: 50M × ~1.2KB ≈ 60GB. Process bị restart cần warm-up. Strategy?
- Q3.3: Bạn cần "atomic snapshot" cho diagnostic dump (toàn bộ store). Như get_map() trong listing 6.12. Có làm được không? Tại sao có thể không nên?

**Round 4 — Beyond locks (10 phút)**
- Q4.1: Ở 200k QPS, lock-based bottleneck ở đâu (theo dự đoán)?
- Q4.2: Lock-free alternative — Michael-Scott queue style cho insert? Cost của RCU/hazard pointer?
- Q4.3: Khi nào BẠN sẽ KHÔNG chuyển sang lock-free? (Họ test xem bạn có quá-engineering không.)

### Đánh giá tự bản thân sau khi xong

| Tiêu chí                           | 1 (kém) – 5 (tốt) | Note                          |
|------------------------------------|-------------------|-------------------------------|
| Hiểu rõ trade-off coarse vs fine   |                   |                               |
| Tính toán bucket count có cơ sở    |                   |                               |
| Phân tích contention realistic     |                   |                               |
| Edge case awareness                |                   |                               |
| Biết khi nào DỪNG optimize         |                   |                               |
| Communicate rõ ràng dưới áp lực    |                   |                               |

---

## Phụ lục — bug pattern bingo card cho lock-based DS

Khi review code lock-based, scan tìm 10 anti-pattern sau:

1. **Check-then-act outside lock**: `if (empty()) ...` rồi mới lock.
2. **Returning reference to internal data**: `T& front()` không có ownership transfer.
3. **Lock held over user callback**: `for_each(f)` gọi `f` trong lock → user lock khác → deadlock.
4. **Holding lock during slow op**: I/O, allocation lớn, sleep, system call.
5. **Wrong lock type**: `unique_lock` ở pure-read path, `shared_lock` ở write path.
6. **Iterator invalidation across lock release**: lưu iterator, unlock, lock lại, dùng iterator.
7. **TOCTOU on container size**: `if (q.size() < N) q.push(...)` — `size()` cũ.
8. **Inconsistent lock order**: function A lock(m1, m2), function B lock(m2, m1).
9. **Missed notification**: predicate không cover hết điều kiện wake.
10. **`notify_*` while still holding the wrong mutex**: technical correctness OK, performance loss.

Tự đánh dấu mỗi lần phát hiện trong code review. Mục tiêu: 10/10 trong 6 tháng.

---

## Hướng dẫn tự chấm

| Đợt làm | Kết quả mong đợi                                                   |
|---------|--------------------------------------------------------------------|
| Lần 1   | Pass W1–W3 + 4/6 essay E. Skeleton bug có thể cần đọc lại sách.    |
| Lần 2   | Pass tất cả essay E + cross-chapter C. Skeleton bug tự tìm < 5 phút.|
| Lần 3   | Mock interview tự trả lời được 80% mà không tra sách.              |

Khi pass lần 3 → đủ điều kiện chuyển sang Chapter 7 (lock-free).
