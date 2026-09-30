#include "Source/app/JobQueue.h"

#include <cstdio>
#include <exception>

namespace icapp {

JobQueue::JobQueue() : worker_([this] { loop(); }) {}

JobQueue::~JobQueue() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stop_ = true;
        pending_.clear();
    }
    wake_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void JobQueue::submit(std::function<void()> work, std::function<void()> done) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_.push_back({std::move(work), std::move(done)});
    }
    wake_.notify_one();
}

void JobQueue::pump() {
    std::deque<std::function<void()>> ready;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ready.swap(finished_);
    }
    for (auto& d : ready)
        if (d) d();
}

void JobQueue::loop() {
    for (;;) {
        Job job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stop_ || !pending_.empty(); });
            if (stop_) return;
            job = std::move(pending_.front());
            pending_.pop_front();
        }
        // O trabalho CONTEM o proprio throw (o `JobScheduler` tem o dele);
        // isto e so a rede de baixo, para a thread nao levar o processo junto.
        try {
            if (job.work) job.work();
        } catch (const std::exception& e) {
            std::fprintf(stderr, "iconcomposer: a job threw: %s\n", e.what());
        } catch (...) {
            std::fprintf(stderr, "iconcomposer: a job threw\n");
        }
        std::lock_guard<std::mutex> lock(mutex_);
        finished_.push_back(std::move(job.done));
    }
}

}  // namespace icapp
