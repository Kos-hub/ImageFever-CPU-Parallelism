#pragma once
#include <future>
#include <vector>
#include <queue>
class ThreadPool
{
public:
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    ThreadPool(size_t nr_threads);
    virtual ~ThreadPool();

    template <class F, class... Args>
    std::future<std::result_of_t<F(Args...)>> enqueue(F&& f, Args &&...args);

private:
    std::vector<std::thread> workers;
    std::queue<std::function<void()>> tasks;

    /* For sync usage, protect the `tasks` queue and `stop` flag. */
    std::mutex mtx;
    std::condition_variable cv;
    bool stop;
};