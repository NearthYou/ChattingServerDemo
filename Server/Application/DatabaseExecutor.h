#pragma once

#include <atomic>
#include <condition_variable>
#include <cstddef>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

class IChatService;

class DatabaseExecutor
{
public:
    using Job = std::function<void(IChatService&)>;

    DatabaseExecutor(IChatService& service, std::size_t queueCapacity);
    ~DatabaseExecutor();

    DatabaseExecutor(const DatabaseExecutor&) = delete;
    DatabaseExecutor& operator=(const DatabaseExecutor&) = delete;

    bool Start();
    bool TrySubmit(Job job);
    void StopAccepting();
    void Stop();

    std::size_t RunningJobCount() const;

private:
    void WorkerLoop();

    IChatService& service;
    const std::size_t queueCapacity;
    mutable std::mutex mutex;
    std::condition_variable condition;
    std::condition_variable startCondition;
    std::deque<Job> jobs;
    std::thread worker;
    bool accepting = false;
    bool stopRequested = false;
    bool startCompleted = false;
    bool startSucceeded = false;
    std::atomic<std::size_t> runningJobs{ 0 };
};
