#include <chrono>
#include <mutex>
#include <thread>

/*
Bài 3 🟡 — Deadlock Demo + Fix
Phần A: Viết một chương trình cố tình tạo deadlock giữa 2 threads và 2 mutexes.
Chứng minh nó treo. Phần B: Fix bằng std::scoped_lock. Phần C: Fix bằng fixed
lock ordering — không dùng scoped_lock.
*/

class PartA {
   private:
    std::mutex m_mx1;
    std::mutex m_mx2;

   public:
    void run() {
        std::thread t1(
            [](std::mutex& m1, std::mutex& m2) {
                std::lock_guard<std::mutex> g1(m1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> g2(m2);
            },
            std::ref(m_mx1), std::ref(m_mx2));
        std::thread t2(
            [](std::mutex& m1, std::mutex& m2) {
                std::lock_guard<std::mutex> g1(m2);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> g2(m1);
            },
            std::ref(m_mx1), std::ref(m_mx2));
        t1.join();
        t2.join();
    }
};

class PartB {
   private:
    std::mutex m_mx1;
    std::mutex m_mx2;

   public:
    void run() {
        std::thread t1(
            [](std::mutex& m1, std::mutex& m2) {
                std::scoped_lock g(m1, m2);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            },
            std::ref(m_mx1), std::ref(m_mx2));
        std::thread t2(
            [](std::mutex& m1, std::mutex& m2) {
                std::scoped_lock g(m2, m1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
            },
            std::ref(m_mx1), std::ref(m_mx2));
        t1.join();
        t2.join();
    }
};

class PartC {
   private:
    std::mutex m_mx1;
    std::mutex m_mx2;

   public:
    void run() {
        std::thread t1(
            [](std::mutex& m1, std::mutex& m2) {
                std::lock_guard<std::mutex> g1(m1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> g2(m2);
            },
            std::ref(m_mx1), std::ref(m_mx2));
        std::thread t2(
            [](std::mutex& m1, std::mutex& m2) {
                std::lock_guard<std::mutex> g1(m1);
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                std::lock_guard<std::mutex> g2(m2);
            },
            std::ref(m_mx1), std::ref(m_mx2));
        t1.join();
        t2.join();
    }
};

int main() {
    PartC test_Obj;
    test_Obj.run();
    return 0;
}
