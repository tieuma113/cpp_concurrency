// ## Bài 4 🔴 — `rw_cache<Key, Value>`: Read-write cache với `shared_mutex` và lazy init
//
// ### Đặc tả hành vi
//
// Viết class template `rw_cache<Key, Value>` với interface:
//
// ```
// find(Key) -> std::optional<Value>   // concurrent reads được phép đồng thời
// update(Key, Value)                  // exclusive write
// get_or_compute(Key, Fn) -> Value    // nếu có thì trả về; nếu chưa thì compute rồi cache
// ```
//
// `get_or_compute(Key, Fn)` phải:
// - Không giữ write lock trong khi `Fn()` đang chạy (Fn có thể tốn thời gian).
// - Đảm bảo `Fn()` chỉ được gọi **đúng một lần** cho mỗi key, ngay cả khi nhiều thread cùng gọi với cùng key lúc cache chưa có.
//
// ### Ràng buộc cứng
//
// - Dùng `std::shared_mutex` + `std::shared_lock` / `std::unique_lock`.
// - `find()` phải dùng shared lock.
// - `update()` phải dùng exclusive lock.
// - **Không dùng `std::call_once`** trong bài này — implement double-checked pattern bằng tay.
//
// ### Invariant cần giữ
//
// Với `get_or_compute`: kể cả khi 100 thread cùng gọi với cùng key lần đầu tiên, `Fn()` chỉ được gọi đúng 1 lần. Kiểm tra bằng `std::atomic<int>` đếm số lần `Fn` được gọi.
//
// ### Debug checkpoint
//
// Đây là bài khó nhất Ch.3. Các lỗi phổ biến:
// - Upgrade từ shared lock lên exclusive lock **không thể** làm trực tiếp — phải release shared lock trước. Không biết điều này sẽ deadlock.
// - Sau khi release shared lock và acquire exclusive lock, phải **check lại** xem key đã được insert chưa. Không check → `Fn()` bị gọi nhiều lần.
// - Đây là double-checked locking pattern — trace logic với 2 thread đồng thời cùng vào `get_or_compute` lần đầu.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao không thể upgrade shared lock → exclusive lock một cách atomic trong C++? Điều gì xảy ra nếu hai thread cùng cố làm điều đó?
// 2. Nếu bỏ double-check sau khi acquire exclusive lock, invariant nào bị phá vỡ? Trace interleaving cụ thể.
// 3. So sánh solution của bạn với dùng `std::call_once` per key — trade-off là gì?
//

