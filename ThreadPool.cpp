#include "ThreadPool.h"

ThreadPool::ThreadPool(size_t nr_threads) : stop(false)
{
    for (size_t i = 0; i < nr_threads; ++i)
        workers.emplace_back(
            [this]
            {
                for (;;)
                {
                    std::function<void()> task;

                    {
                        std::unique_lock<std::mutex> lock(this->mtx);
                        this->cv.wait(lock,
                            [this] { return this->stop || !this->tasks.empty(); });
                        if (this->stop && this->tasks.empty())
                            return;
                        task = std::move(this->tasks.front());
                        this->tasks.pop();
                    }

                    task();
                }
            }
            );
}

ThreadPool::~ThreadPool()
{
    /* stop thread pool, and notify all threads to finish the remained tasks. */
    {
        std::unique_lock<std::mutex> lock(mtx);
        stop = true;
    }
    cv.notify_all();
    for (auto& worker : workers)
        worker.join();
}
