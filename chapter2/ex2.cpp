#include <chrono>
#include <iostream>
#include <thread>
#include <vector>

/*Thread registry
    Write a program that will spawn multiple threads
    print the threadid of the spawned threads
    Main thread must stored all the thread id to std::vector<std::thread::id> and print out after all the thread is finished
*/
class scoped_thread {
public: 
    scoped_thread(std::thread& _t) {
        my_thread = std::move(_t);
    }

    ~scoped_thread() {
        if (my_thread.joinable()) {
            std::cout << "Join thread ID: " << my_thread.get_id() << "\n";
            my_thread.detach();
        }
    }
    scoped_thread(const scoped_thread&) = delete;
    scoped_thread(scoped_thread&&) = delete;
    scoped_thread& operator=(scoped_thread &other) = delete;
    scoped_thread& operator=(scoped_thread &&other) = delete;
private:
    std::thread my_thread;
};


class thread_registry {
public:
    thread_registry(): mv_threadIdList() {};
    ~thread_registry() {
        for (const auto& id : mv_threadIdList) {
            std::cout << "thread ID: " << id << "\n";
        }
    } 
    static void print_thread_id() {
        std::cout << std::this_thread::get_id() << "\n";
        std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    }

    void run() {
        int max_thread = std::thread::hardware_concurrency() != 0 ? std::thread::hardware_concurrency() : 2; 
        std::cout << "Max thread = " << max_thread << "\n";
        for (int i = 0; i < max_thread; i++) {
            std::thread new_thread(print_thread_id);
            mv_threadIdList.push_back(new_thread.get_id());
            scoped_thread g(new_thread);
        } 
    } 
private:
    std::vector<std::thread::id> mv_threadIdList;
};

int main() {
    thread_registry rg;
    rg.run();
    return 0;
}
