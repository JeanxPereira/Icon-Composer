#pragma once
// Uma thread de trabalho e uma fila. `submit` entrega o trabalho, que roda na
// thread; o `done` volta para a thread principal e roda dentro de `pump()`.
//
// Uma raia so, de proposito: o unico cliente e o `JobScheduler` (Ports.h),
// que ja garante um render de cada vez sobre o `rb::Device`. Uma segunda
// thread nao teria o que fazer.
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace icapp {

class JobQueue {
public:
    JobQueue();
    // Termina o trabalho em curso e descarta o que ainda esperava; os `done`
    // pendentes nao rodam.
    ~JobQueue();
    JobQueue(const JobQueue&) = delete;
    JobQueue& operator=(const JobQueue&) = delete;

    void submit(std::function<void()> work, std::function<void()> done);
    // Na thread principal, uma vez por quadro.
    void pump();

private:
    struct Job {
        std::function<void()> work, done;
    };
    void loop();

    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<Job> pending_;
    std::deque<std::function<void()>> finished_;
    bool stop_ = false;
    std::thread worker_;
};

}  // namespace icapp
