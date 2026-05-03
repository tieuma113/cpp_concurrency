// ## Bài 8 🟡 — `fine_queue<T>`: Queue với dummy node và tách head/tail mutex
//
// ### Đặc tả hành vi
//
// Viết class template `fine_queue<T>` dưới dạng singly linked list, với hai mutex tách biệt bảo vệ head và tail. Queue sử dụng kỹ thuật **dummy node** để tách biệt hoàn toàn vùng truy cập của push và pop.
//
// Interface:
//
// ```
// push(T value)
// try_pop() -> std::shared_ptr<T>
// try_pop(T& out) -> bool
// empty() -> bool
// ```
//
// Yêu cầu thiết kế:
// 1. Linked list nội bộ luôn có ít nhất một node (dummy node). Queue rỗng ⟺ `head == tail`, cả hai trỏ vào dummy node.
// 2. `push()` chỉ lock `tail_mutex`. `try_pop()` lock `head_mutex`, và chỉ lock `tail_mutex` trong thời gian rất ngắn để đọc `tail`.
// 3. Data được lưu bằng `std::shared_ptr<T>` trong node. Dummy node có `data == nullptr`.
// 4. `push()` phải thực hiện memory allocation (`make_shared`, `new node`) **trước** khi acquire lock.
// 5. Mỗi node sở hữu node tiếp theo qua `std::unique_ptr<node>`.
//
// ### Ràng buộc cứng
//
// - **Không dùng `std::queue`**, `std::deque`, hay bất kỳ container nào bên trong. Tự viết linked list.
// - **Không dùng condition variable** trong bài này — chỉ pure try-based interface.
// - Đúng hai mutex: `head_mutex` và `tail_mutex`. Không thêm mutex thứ ba.
// - `tail` là raw pointer (`node*`), `head` là `std::unique_ptr<node>`.
//
// ### Invariant cần giữ
//
// - `tail->next == nullptr` luôn đúng.
// - `tail->data == nullptr` luôn đúng (tail luôn là dummy).
// - `head == tail` ⟺ queue rỗng.
// - Với mỗi node `x` trong list mà `x != tail`: `x->data != nullptr` và `x->next` trỏ đến node tiếp theo.
// - Đi theo `next` từ `head` cuối cùng đến `tail`.
//
// ### Bài test bắt buộc
//
// 4 producer threads, mỗi thread push 50,000 giá trị (`int`). 4 consumer threads, mỗi thread `try_pop` trong loop cho đến khi tổng cộng đã pop 200,000 elements (dùng `std::atomic<int>` đếm). Sau khi join: tổng giá trị pop ra phải bằng tổng giá trị đã push.
//
// ### Debug checkpoint
//
// - Chạy `-fsanitize=thread`. Nếu có report → sai. Đặc biệt chú ý data race trên `tail` — đọc `tail` trong `try_pop` **phải** lock `tail_mutex`.
// - Nếu `get_tail()` được gọi **ngoài** scope của `head_mutex` → có thể `head` bị di chuyển quá `tail` cũ. Trace interleaving cụ thể.
// - Khi queue có đúng 1 element: `head->next` là dummy, `head != tail`. Pop phải đưa `head` sang dummy. Sau pop: `head == tail`, queue rỗng.
//
// ### Câu hỏi phân tích
//
// 1. Tại sao dummy node giải quyết được vấn đề `push` và `pop` cùng truy cập `head->next` / `tail->next` trên cùng một node? Trace trường hợp queue 1 phần tử **không có** dummy node.
// 2. Tại sao `get_tail()` phải được gọi **bên trong** scope của `head_mutex`? Nếu gọi ngoài, viết interleaving 2 thread gây sai.
// 3. `push()` thực hiện allocation ngoài lock — điều này cải thiện concurrency cụ thể như thế nào so với `std::queue`-based implementation?
// 4. Giải thích tại sao `tail` là raw pointer mà `head` là `unique_ptr`. Nếu đổi `tail` thành `unique_ptr` thì sao?
//

