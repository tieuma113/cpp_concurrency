#include <iostream>
#include <stdexcept>
#include <thread>
#include <exception>
#include <utility>



/*Implemented scoped_thread that:
    Constructor will take a std::thread and take it owner ship
    if the thread not joinable -> throw a std::logic_error
    destructor will auto join can't copy
*/
class scoped_thread {
public: 
    scoped_thread(std::thread& _t) {
        my_thread = std::move(_t);
    }

    ~scoped_thread() {
        if (my_thread.joinable()) {
            my_thread.join();
        }
    }
    scoped_thread(const scoped_thread&) = delete;
    scoped_thread(scoped_thread&&) = delete;
    scoped_thread& operator=(scoped_thread &other) = delete;
    scoped_thread& operator=(scoped_thread &&other) = delete;
private:
    std::thread my_thread;
};

void job() {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
}

void test(bool throw_able) {
    std::thread t1(job);
    scoped_thread g(t1);
    if (throw_able) {
        throw std::runtime_error("throw\n");
        return;
    }
}

int main() {
    try {
     test(true);
     } catch (std::exception& e) {
        std::cout << e.what() << std::endl; 
     } 
    
    return 0;
}
