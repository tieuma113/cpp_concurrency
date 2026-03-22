#include <iostream>
#include <mutex>
#include <thread>
#include <vector>


/*
Bài 1 🟢 — Thread-safe Counter
Viết class Counter thread-safe với:

void increment()
void decrement()
int get() const

Test: 4 threads, mỗi thread increment 100,000 lần. Kết quả cuối phải chính xác là 400,000.
*/

class counter {
    public:
        counter() : m_count{0} {}
        void increase(unsigned long const inc) {
            for (int i = 0; i < inc; ++i) {
                std::lock_guard<std::mutex> g(m_mx_lock);
                m_count++;
            }
        }
        void decrease(unsigned long const dec) {
            for (int i = 0; i < dec; i++){
                std::lock_guard<std::mutex> g(m_mx_lock);
                m_count--;
            }
        }

        const int get() {
            std::lock_guard<std::mutex> g(m_mx_lock);
            return m_count;
        }

    private:
        std::mutex m_mx_lock;
        unsigned long m_count;
};

void run(counter &c) {
    c.increase(100000);
}

int main() {
    std::vector<std::thread> thread_list(4);
    counter c;
    for (int i = 0; i < 4; ++i) {
        thread_list[i] = std::thread(run, std::ref(c));
    }
    for (auto& i: thread_list) {
        if (i.joinable()) i.join();
    }
    std::cout << c.get() << "\n";
    return 0;
}
