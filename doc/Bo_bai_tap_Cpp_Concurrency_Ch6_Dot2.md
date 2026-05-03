# PHẦN A — Fine-Grained Locking Queue

---

## Bài 8 🟡 — `fine_queue<T>`: Queue với dummy node và tách head/tail mutex

### Đặc tả hành vi

Viết class template `fine_queue<T>` dưới dạng singly linked list, với hai mutex tách biệt bảo vệ head và tail. Queue sử dụng kỹ thuật **dummy node** để tách biệt hoàn toàn vùng truy cập của push và pop.

Interface:

```
push(T value)
try_pop() -> std::shared_ptr<T>
try_pop(T& out) -> bool
empty() -> bool
```

Yêu cầu thiết kế:
1. Linked list nội bộ luôn có ít nhất một node (dummy node). Queue rỗng ⟺ `head == tail`, cả hai trỏ vào dummy node.
2. `push()` chỉ lock `tail_mutex`. `try_pop()` lock `head_mutex`, và chỉ lock `tail_mutex` trong thời gian rất ngắn để đọc `tail`.
3. Data được lưu bằng `std::shared_ptr<T>` trong node. Dummy node có `data == nullptr`.
4. `push()` phải thực hiện memory allocation (`make_shared`, `new node`) **trước** khi acquire lock.
5. Mỗi node sở hữu node tiếp theo qua `std::unique_ptr<node>`.

### Ràng buộc cứng

- **Không dùng `std::queue`**, `std::deque`, hay bất kỳ container nào bên trong. Tự viết linked list.
- **Không dùng condition variable** trong bài này — chỉ pure try-based interface.
- Đúng hai mutex: `head_mutex` và `tail_mutex`. Không thêm mutex thứ ba.
- `tail` là raw pointer (`node*`), `head` là `std::unique_ptr<node>`.

### Invariant cần giữ

- `tail->next == nullptr` luôn đúng.
- `tail->data == nullptr` luôn đúng (tail luôn là dummy).
- `head == tail` ⟺ queue rỗng.
- Với mỗi node `x` trong list mà `x != tail`: `x->data != nullptr` và `x->next` trỏ đến node tiếp theo.
- Đi theo `next` từ `head` cuối cùng đến `tail`.

### Bài test bắt buộc

4 producer threads, mỗi thread push 50,000 giá trị (`int`). 4 consumer threads, mỗi thread `try_pop` trong loop cho đến khi tổng cộng đã pop 200,000 elements (dùng `std::atomic<int>` đếm). Sau khi join: tổng giá trị pop ra phải bằng tổng giá trị đã push.

### Debug checkpoint

- Chạy `-fsanitize=thread`. Nếu có report → sai. Đặc biệt chú ý data race trên `tail` — đọc `tail` trong `try_pop` **phải** lock `tail_mutex`.
- Nếu `get_tail()` được gọi **ngoài** scope của `head_mutex` → có thể `head` bị di chuyển quá `tail` cũ. Trace interleaving cụ thể.
- Khi queue có đúng 1 element: `head->next` là dummy, `head != tail`. Pop phải đưa `head` sang dummy. Sau pop: `head == tail`, queue rỗng.

### Câu hỏi phân tích

1. Tại sao dummy node giải quyết được vấn đề `push` và `pop` cùng truy cập `head->next` / `tail->next` trên cùng một node? Trace trường hợp queue 1 phần tử **không có** dummy node.
2. Tại sao `get_tail()` phải được gọi **bên trong** scope của `head_mutex`? Nếu gọi ngoài, viết interleaving 2 thread gây sai.
3. `push()` thực hiện allocation ngoài lock — điều này cải thiện concurrency cụ thể như thế nào so với `std::queue`-based implementation?
4. Giải thích tại sao `tail` là raw pointer mà `head` là `unique_ptr`. Nếu đổi `tail` thành `unique_ptr` thì sao?

---

## Bài 9 🔴 — `blocking_fine_queue<T>`: Thêm wait_and_pop + shutdown lên fine-grained queue

### Đặc tả hành vi

Mở rộng `fine_queue` (Bài 8) thành `blocking_fine_queue<T>` với blocking interface:

```
push(T value)
try_pop() -> std::shared_ptr<T>
try_pop(T& out) -> bool
wait_and_pop() -> std::shared_ptr<T>
wait_and_pop(T& out)
empty() -> bool
shutdown()
```

`wait_and_pop()`:
- Block nếu queue rỗng. Thread phải ngủ thật (condition variable), không busy-wait.
- Predicate: `head.get() != get_tail()` (queue không rỗng) **hoặc** shutdown flag đã set.
- Nếu được unblock do shutdown và queue vẫn rỗng: ném `queue_shutdown` exception.

`shutdown()`:
- Set flag, `notify_all()`.
- Sau shutdown: `push()` ném `queue_shutdown`. `try_pop()` vẫn hoạt động bình thường cho đến khi queue cạn.

**Exception safety trong wait_and_pop():**
- Overload `wait_and_pop(T& out)` phải thực hiện `value = std::move(*head->data)` **trước** khi remove node khỏi list. Nếu copy/move throw, node vẫn còn trong queue.

### Ràng buộc cứng

- Dùng `std::condition_variable` + `head_mutex` cho wait.
- `notify_one()` trong `push()`, `notify_all()` trong `shutdown()`.
- `notify_one()` phải được gọi **sau** khi đã release `tail_mutex` — giải thích tại sao.
- Vẫn giữ hai mutex tách biệt như Bài 8.

### Invariant cần giữ

Tất cả invariant của Bài 8, cộng thêm:
- Sau `shutdown()`, mỗi thread đang block ở `wait_and_pop` phải eventually được unblock.
- Không element nào bị mất: mọi element đã push trước shutdown đều pop được.

### Bài test bắt buộc

**Test 1 — Normal flow:** 3 producers × 1000 items, 5 consumers dùng `wait_and_pop`. Khi đủ 3000 items consumed, main thread gọi `shutdown()`. Verify: tổng giá trị đúng, không thread nào bị block mãi.

**Test 2 — Shutdown unblocks waiters:** Launch 5 consumer threads trước khi có bất kỳ producer nào. Tất cả 5 threads phải block. Sau 100ms, gọi `shutdown()`. Tất cả 5 threads phải catch `queue_shutdown` và kết thúc trong vòng 200ms.

**Test 3 — Exception safety:** Push 10 items. Pop 5 bằng `try_pop`. Pop item 6 bằng `wait_and_pop(T&)` với `T` có copy/move constructor throw exception. Queue phải vẫn còn đúng 5 items.

### Debug checkpoint

- `wait_for_data()` helper function phải return `std::unique_lock<std::mutex>` — tại sao không phải `lock_guard`?
- Nếu `notify_one()` trong `push()` được gọi khi `tail_mutex` vẫn đang lock: thread consumer wakeup, cần lock `head_mutex`, rồi trong predicate gọi `get_tail()` cần lock `tail_mutex` → có bị block không?
- Nếu dùng `notify_one()` trong `shutdown()` thay vì `notify_all()` — 4/5 consumer threads sẽ block mãi.

### Câu hỏi phân tích

1. `wait_for_data()` trả `std::unique_lock` qua return — giải thích flow ownership của lock từ `wait_for_data()` → `wait_pop_head()` → caller.
2. Tại sao không dùng `data_cond.wait(lk, [&]{ return !empty(); })` làm predicate? (Hint: `empty()` acquire lock riêng.)
3. Khi exception xảy ra trong `wait_and_pop()` (ví dụ `make_shared` throw), thread đó không gọi `notify_one()` → thread khác đang chờ sẽ bỏ lỡ notification. Giải pháp là gì?
4. So sánh thiết kế này với `threadsafe_queue` single-mutex (Bài 5 cũ): concurrency đạt được ở những điểm cụ thể nào?

---

# PHẦN B — Hash Map với Per-Bucket Locking

---

## Bài 10 🟡 — `concurrent_map<K,V>`: Thread-safe hash map với per-bucket `shared_mutex`

### Đặc tả hành vi

Viết class template `concurrent_map<K,V,Hash>` dựa trên hash table cố định số bucket, mỗi bucket được bảo vệ bởi `std::shared_mutex` riêng.

Interface:

```
Value get(Key const& key, Value const& default_value = Value()) const
void put(Key const& key, Value const& value)       // add hoặc update
bool remove(Key const& key)                         // trả về true nếu đã xóa
std::map<Key,Value> snapshot() const                 // copy toàn bộ state
size_t size() const                                  // tổng số entries
```

Thiết kế nội bộ:
- Số bucket cố định khi construct (mặc định 19, prime number).
- Mỗi bucket là một `std::list<std::pair<Key,Value>>`, có `std::shared_mutex` riêng.
- `get()` dùng `shared_lock` (cho phép nhiều reader đồng thời trên cùng bucket).
- `put()` và `remove()` dùng `unique_lock`.
- `snapshot()` lock **tất cả** bucket theo thứ tự index tăng dần, rồi copy.

### Ràng buộc cứng

- **Không dùng `std::unordered_map`** hay bất kỳ associative container nào bên trong. Tự quản lý bucket + list.
- Bucket count cố định — không rehash.
- `Hash` là template parameter mặc định `std::hash<K>`.
- Buckets được lưu trong `std::vector<std::unique_ptr<bucket_type>>` — giải thích tại sao `unique_ptr` chứ không phải trực tiếp `bucket_type`.

### Invariant cần giữ

- Mỗi key xuất hiện tối đa một lần trong toàn bộ map.
- `get_bucket(key)` luôn trả về cùng một bucket cho cùng key (deterministic hash).
- `snapshot()` trả về consistent view: không thấy nửa operation nào.

### Bài test bắt buộc

**Test 1 — Concurrent read/write:** 4 writer threads, mỗi thread `put` 10,000 key-value pairs (keys = thread_id * 10000 + i). 4 reader threads, mỗi thread `get` các key ngẫu nhiên trong phạm vi tổng. Không data race.

**Test 2 — Concurrent put + remove:** 2 writer threads put, 2 remover threads remove cùng range. Sau khi join: `snapshot()` phải consistent (mỗi key hoặc tồn tại hoặc không, không có partial state).

**Test 3 — Snapshot consistency:** Thread A liên tục `put(1, v++)`. Thread B gọi `snapshot()` lặp lại. Mỗi snapshot phải thấy đúng 1 entry cho key 1 với một giá trị cụ thể — không bao giờ thấy key 1 hai lần hay thiếu.

### Debug checkpoint

- `snapshot()` lock tất cả bucket — nếu lock không theo thứ tự cố định → deadlock khi hai thread gọi `snapshot()` đồng thời.
- Nếu dùng `bucket_type` trực tiếp trong vector (không qua `unique_ptr`): `std::shared_mutex` không moveable → vector không thể resize (kể cả khi bạn không resize, constructor sẽ fail).
- `find_entry_for()` helper dùng `std::find_if` trên bucket list — hàm này phải nhận `Key const&`, không copy key.

### Câu hỏi phân tích

1. Tại sao hash table được chọn thay vì binary tree hay sorted array cho fine-grained locking? Phân tích access pattern của từng cấu trúc.
2. `get()` dùng `shared_lock`, `put()` dùng `unique_lock` — nếu 10 thread cùng `get` trên cùng bucket, bao nhiêu thread chạy đồng thời? Nếu 1 thread `put` và 9 thread `get` trên cùng bucket thì sao?
3. `snapshot()` phải lock tất cả bucket — đây là operation duy nhất cần multiple locks. Giải thích tại sao lock theo index tăng dần là đủ để ngăn deadlock.
4. Nếu cho phép rehash (tăng số bucket khi load factor cao), concurrency bị ảnh hưởng thế nào? Cần thêm cơ chế đồng bộ gì?

---

## Bài 11 🔴 — `lru_concurrent_cache<K,V>`: LRU eviction trên concurrent hash map

### Đặc tả hành vi

Kết hợp `concurrent_map` (Bài 10) với LRU (Least Recently Used) eviction policy.

Interface:

```
// Constructor: max_entries là capacity
lru_concurrent_cache(size_t max_entries, size_t num_buckets = 19)

std::optional<Value> get(Key const& key)        // trả về value + đánh dấu recently used
void put(Key const& key, Value const& value)    // insert/update + evict LRU nếu full
bool remove(Key const& key)
size_t size() const
```

Hành vi LRU:
- Mỗi lần `get()` hoặc `put()` thành công trên một key, key đó được đánh dấu là "most recently used".
- Khi `put()` gây vượt capacity, entry "least recently used" bị evict trước khi insert entry mới.
- Eviction phải thread-safe và không gây deadlock.

### Ràng buộc cứng

- Dùng hash map (tương tự Bài 10) cho O(1) lookup.
- Dùng một doubly-linked list riêng để track thứ tự access (LRU order).
- LRU list cần mutex riêng, tách biệt với bucket mutexes.
- **Không dùng `std::unordered_map`** — tự quản lý.
- Eviction trong `put()` phải release bucket lock trước khi acquire LRU lock (nếu cần) để tránh lock ordering violation.

### Invariant cần giữ

- `size() <= max_entries` luôn đúng.
- Mỗi key trong hash map có đúng một node tương ứng trong LRU list và ngược lại.
- Entry bị evict luôn là entry ít được truy cập nhất (tail của LRU list).

### Bài test bắt buộc

**Test 1 — Eviction correctness:** Cache capacity = 100. Insert keys 0–199 tuần tự. Sau đó `get(0)` phải trả về `nullopt` (đã bị evict), `get(100)` phải có giá trị (chưa bị evict).

**Test 2 — Concurrent put + eviction:** 4 threads, mỗi thread `put` 10,000 entries vào cache capacity = 5000. Tại mọi thời điểm `size() <= 5000`. Sau khi join: `size() <= 5000`.

**Test 3 — get promotes access order:** Insert keys 0–99 (capacity = 100). `get(0)` rồi `put(100, v)`. Key 1 phải bị evict (LRU), key 0 phải còn tồn tại (vừa được access).

### Debug checkpoint

- Lock ordering: nếu hold bucket lock rồi acquire LRU lock, thread khác hold LRU lock rồi acquire bucket lock (trong eviction) → **deadlock**. Phải thiết kế protocol rõ ràng.
- Eviction cần: (1) lock LRU list → lấy victim key → unlock LRU list → (2) lock victim's bucket → remove entry → unlock bucket → (3) lock LRU list → remove LRU node. Giữa bước 1 và 2, thread khác có thể access victim key → phải check lại.
- `size()` cần đồng bộ — nếu dùng `std::atomic<size_t>`, increment/decrement phải diễn ra đúng thời điểm.

### Câu hỏi phân tích

1. Thiết kế lock ordering protocol: liệt kê tất cả mutex trong hệ thống và thứ tự acquire cho từng operation (`get`, `put`, `put+evict`, `remove`).
2. Trong eviction, giữa lúc chọn victim (LRU tail) và lúc xóa victim khỏi bucket, thread khác có thể `get(victim_key)` → promote victim lên MRU. Thiết kế của bạn xử lý race này thế nào?
3. So sánh "global LRU lock" vs "per-bucket LRU" — trade-off giữa correctness, performance, và complexity.
4. Nếu eviction cần xóa entry đang được thread khác `get()` đọc, `shared_ptr` giúp gì ở đây?

---

# PHẦN C — Hand-Over-Hand Locking

---

## Bài 12 🟡 — `fine_list<T>`: Linked list với per-node locking

### Đặc tả hành vi

Viết class template `fine_list<T>` — singly linked list với mutex trên mỗi node, hỗ trợ concurrent traversal bằng kỹ thuật **hand-over-hand locking** (lock coupling).

Interface:

```
void push_front(T const& value)

template<typename F>
void for_each(F fn)

template<typename P>
std::shared_ptr<T> find_first_if(P predicate)

template<typename P>
void remove_if(P predicate)

bool empty() const
size_t size() const
```

Kỹ thuật hand-over-hand:
- Khi traverse list: lock node hiện tại → lock node tiếp theo → unlock node hiện tại → tiến tới. Tại mọi thời điểm, luôn giữ lock trên ít nhất một node.
- Head là **sentinel node** (dummy) không chứa data, chỉ giữ mutex cho entry point.
- Data trong mỗi node lưu bằng `std::shared_ptr<T>`.

### Ràng buộc cứng

- Mỗi node có `std::mutex` riêng.
- Dùng `std::unique_lock` (không phải `lock_guard`) — vì cần unlock thủ công.
- `push_front()` chỉ lock head's mutex.
- `for_each()`, `find_first_if()`, `remove_if()` đều dùng hand-over-hand.
- Destructor phải xóa tất cả nodes — dùng `remove_if([](auto&){return true;})`.

### Invariant cần giữ

- Traversal luôn đi một chiều (head → tail), luôn lock next trước khi unlock current → **không deadlock**.
- Nhiều thread có thể traverse đồng thời, miễn đang ở các node khác nhau.
- `remove_if` xóa node `N`: phải hold lock trên node trước `N` để ngăn thread khác đến `N`.

### Bài test bắt buộc

**Test 1 — Concurrent push + for_each:** 4 threads push 1000 items mỗi thread. Đồng thời 2 threads chạy `for_each` đếm elements. Không crash, không data race.

**Test 2 — Concurrent remove + find:** Push 10,000 items (values 0–9999). 2 threads `remove_if(x < 5000)`. 2 threads `find_first_if(x == 7777)`. Sau khi join: tất cả items < 5000 đã bị xóa, items ≥ 5000 còn nguyên.

**Test 3 — Ordering safety:** 3 threads cùng `remove_if` với các predicate khác nhau trên cùng list. Không deadlock, không double-free, không missing elements.

### Debug checkpoint

- Trong `remove_if`: sau khi xóa node, **không** advance `current` pointer — phải check node mới tại vị trí cũ (vì `current->next` đã đổi).
- Xóa node = move `current->next` vào local variable, rồi gán `current->next = old_next->next`. Node bị xóa khi `unique_ptr` local ra khỏi scope. **Phải unlock** node bị xóa trước khi nó bị destroy — destroying locked mutex là UB.
- Verify: lock trên node bị xóa được release **trước** `unique_ptr` destructor chạy. Trong code: `next_lk.unlock()` rồi mới để `old_next` ra khỏi scope.

### Câu hỏi phân tích

1. Hand-over-hand locking đảm bảo no-deadlock bằng cách nào? (Hint: chứng minh total order trên lock acquisition.)
2. Tại sao threads không thể "vượt" nhau? Thread A ở node 5, thread B ở node 3 — B có thể đến node 5 trước khi A rời không?
3. `push_front` chỉ lock head — nếu thread A đang `for_each` vừa lock head xong, thread B gọi `push_front`, điều gì xảy ra?
4. So sánh throughput: fine_list vs single-mutex list khi 8 threads cùng `for_each` trên list 10,000 nodes, mỗi `fn` tốn 1ms. Ước tính lý thuyết.

---

## Bài 13 🔴 — `sorted_fine_list<T>`: Sorted linked list với concurrent insert

### Đặc tả hành vi

Mở rộng `fine_list` thành **sorted linked list** (tăng dần), hỗ trợ insert đúng vị trí và maintain sorted order dưới concurrent access.

Interface:

```
void insert(T const& value)             // insert vào đúng vị trí sorted
bool contains(T const& value) const     // tìm kiếm
bool remove(T const& value)             // xóa entry đầu tiên bằng value

template<typename F>
void for_each(F fn)                     // traverse sorted order

size_t size() const
```

`insert(value)`:
- Traverse list bằng hand-over-hand locking cho đến khi tìm vị trí đúng: node hiện tại ≤ value < node tiếp theo.
- Insert node mới giữa current và next.
- Trong lúc insert, phải hold lock trên **cả** predecessor và successor.

### Ràng buộc cứng

- Sorted order (ascending) dùng `operator<` trên `T`.
- Insert phải dùng hand-over-hand + lock predecessor khi chèn.
- Không dùng `std::set`, `std::list` (ngoài internal node struct).
- Giữ sentinel head (data-less) như `fine_list`.
- `T` phải là `CopyConstructible` và `LessThanComparable`.

### Invariant cần giữ

- Tại mọi thời điểm mà không có thread nào đang hold lock, list ở trạng thái sorted.
- Concurrent inserts không phá vỡ sorted order — kể cả khi hai thread insert vào cùng vị trí.
- `remove(value)` xóa đúng **một** occurrence đầu tiên.

### Bài test bắt buộc

**Test 1 — Concurrent sorted insert:** 8 threads, mỗi thread insert 5000 giá trị ngẫu nhiên (range 0–99999). Sau khi join: `for_each` verify list sorted ascending và `size() == 40000`.

**Test 2 — Concurrent insert + remove:** 4 threads insert, 4 threads remove giá trị ngẫu nhiên. Sau khi join: list vẫn sorted. Tổng insert - tổng remove thành công = `size()`.

**Test 3 — Duplicate values:** Insert 1000, 1000, 1000 từ 3 threads. `size()` phải = 3. Remove 1000 một lần → `size()` = 2. List vẫn sorted.

### Debug checkpoint

- Insert tại vị trí giữa predecessor và successor: phải hold lock trên **cả hai** để ngăn thread khác insert vào cùng gap. Nếu chỉ lock predecessor → race condition với thread khác cùng chọn gap đó.
- Insert tại tail (value lớn nhất): successor là nullptr. Không cần lock successor. Nhưng phải verify predecessor vẫn là tail node.
- Nếu hai threads cùng traverse đến cùng gap (pred → succ), thread A lock trước, insert A giữa pred → succ. Thread B cần re-check vì giờ pred → A → succ. B phải advance qua A.

### Câu hỏi phân tích

1. Insert cần lock cả predecessor và successor — nhưng hand-over-hand chỉ hold 2 locks tại một thời điểm (current + next). Insert thực sự cần bao nhiêu locks? Trace step-by-step.
2. Nếu cho phép duplicate values, hai threads insert cùng value vào cùng vị trí: trace interleaving và chứng minh sorted order được giữ.
3. `remove` cần hold lock trên predecessor để thay đổi `predecessor->next`. Nếu không lock predecessor, interleaving nào gây ra bug?
4. So sánh `sorted_fine_list` vs `concurrent_map` (Bài 10) cho use case "concurrent set": trade-off về lookup time, insert time, memory, và concurrency level.

---

# PHẦN D — Bounded Queue (Extension)

---

## Bài 14 🟡 — `bounded_queue<T>`: Queue có capacity limit, back-pressure khi full

### Đặc tả hành vi

Viết class template `bounded_queue<T>` — thread-safe queue với giới hạn số lượng element tối đa. Khi queue đầy, `push` block cho đến khi có chỗ trống.

Interface:

```
// Constructor
bounded_queue(size_t capacity)

// Blocking operations
void push(T value)               // block nếu full, unblock khi có slot trống
void pop(T& out)                 // block nếu empty, unblock khi có element

// Non-blocking
bool try_push(T value)           // false nếu full
bool try_pop(T& out)             // false nếu empty

// Control
void shutdown()                  // unblock tất cả, fail mọi operation sau đó
size_t size() const
size_t capacity() const
```

`push()` block:
- Khi `size() == capacity()`, thread gọi `push` phải ngủ (condition variable).
- Khi thread khác `pop` thành công → notify một thread đang chờ push.

`pop()` block:
- Khi queue rỗng, thread gọi `pop` phải ngủ.
- Khi thread khác `push` thành công → notify một thread đang chờ pop.

### Ràng buộc cứng

- Dùng **hai condition variables**: `not_full` (cho push waiters) và `not_empty` (cho pop waiters).
- Dùng một `std::mutex` (single mutex — không cần fine-grained cho bài này).
- Bên trong dùng circular buffer hoặc `std::queue` + size counter — tự chọn.
- `shutdown()` phải unblock **cả** push waiters và pop waiters.

### Invariant cần giữ

- `0 <= size() <= capacity()` luôn đúng.
- Mỗi `push` thành công tăng size đúng 1, mỗi `pop` thành công giảm size đúng 1.
- FIFO ordering: elements pop ra theo thứ tự push vào.

### Bài test bắt buộc

**Test 1 — Back-pressure:** Capacity = 5. Producer thread push 100 items với sleep 0ms. Consumer thread pop với sleep 10ms. Queue size không bao giờ vượt 5. Producer phải bị block phần lớn thời gian.

**Test 2 — Balanced throughput:** Capacity = 50. 3 producers × 10,000 items. 3 consumers pop liên tục. Sau khi đủ 30,000 items consumed: `shutdown()`. Tổng giá trị đúng.

**Test 3 — Shutdown unblocks both sides:** Capacity = 5. Fill queue đầy (5 items). Launch producer thread (sẽ block vì full). Launch consumer thread trên empty aux queue (sẽ block vì empty). Gọi `shutdown()` — cả hai threads phải unblock và kết thúc.

### Debug checkpoint

- Hai condition variables cùng dùng một mutex — điều này hợp lệ trong C++. Cả hai `wait()` đều lock cùng mutex.
- `push()` trong predicate phải check cả `shutdown_flag` — nếu không, shutdown sẽ không unblock push waiters.
- Sau `pop()` thành công, gọi `not_full.notify_one()` để đánh thức push waiter. Nếu quên → producer block mãi khi queue đã từng full.

### Câu hỏi phân tích

1. Tại sao cần **hai** condition variables? Nếu dùng một condition variable cho cả push waiters và pop waiters, vấn đề gì xảy ra? (Hint: `notify_one()` đánh thức sai loại waiter.)
2. Bounded queue cung cấp "back-pressure" — giải thích điều này ngăn chặn vấn đề gì trong producer-consumer pattern so với unbounded queue.
3. Nếu dùng `notify_all()` thay vì `notify_one()` trong `push()` cho `not_empty`: performance bị ảnh hưởng thế nào khi có 100 consumer threads?
4. Circular buffer vs `std::queue` bên trong — trade-off memory allocation, cache locality, và implementation complexity.

---

# Thứ tự làm đề xuất

```
Bài 8  (Fine-grained queue — foundation)
    ↓
Bài 9  (Blocking fine queue — extends Bài 8)
    ↓
Bài 12 (Hand-over-hand list — new technique)
    ↓
Bài 10 (Concurrent hash map — per-bucket locking)
    ↓
Bài 14 (Bounded queue — dual condition variable)
    ↓
Bài 13 (Sorted list — hard, extends Bài 12)
    ↓
Bài 11 (LRU cache — synthesis, hardest)
```

---

# Checklist "done" cho mỗi bài

- [ ] Compile sạch với `-Wall -Wextra`
- [ ] Chạy sạch với `-fsanitize=thread` (không có data race report)
- [ ] Chạy sạch với `-fsanitize=address` (không có memory error)
- [ ] Tất cả test cases pass, bao gồm exception paths
- [ ] Có thể trả lời tất cả câu hỏi phân tích bằng lời, không nhìn code
- [ ] Có thể trace worst-case interleaving mà thiết kế vẫn đúng
- [ ] Giải thích được lock ordering và chứng minh no-deadlock cho design
