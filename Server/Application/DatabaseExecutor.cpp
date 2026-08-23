#include "DatabaseExecutor.h"

#include "IChatService.h"

#include <algorithm>
#include <utility>

DatabaseExecutor::DatabaseExecutor(IChatService& chatService, std::size_t capacity)
    : service(chatService),
      queueCapacity(std::max<std::size_t>(1, capacity))
{
}

DatabaseExecutor::~DatabaseExecutor()
{
    Stop();
}

bool DatabaseExecutor::Start()
{
    std::unique_lock<std::mutex> lock(mutex);
    if (worker.joinable())
    {
        return startSucceeded;
    }

    stopRequested = false;
    startCompleted = false;
    startSucceeded = false;
    worker = std::thread([this] { WorkerLoop(); });
    startCondition.wait(lock, [this] { return startCompleted; });
    const bool succeeded = startSucceeded;
    lock.unlock();

    if (!succeeded && worker.joinable())
    {
        worker.join();
    }
    return succeeded;
}

bool DatabaseExecutor::TrySubmit(Job job)
{
    if (!job)
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex);
    if (!accepting || stopRequested || jobs.size() >= queueCapacity)
    {
        return false;
    }
    jobs.push_back(std::move(job));
    condition.notify_one();
    return true;
}

void DatabaseExecutor::StopAccepting()
{
    std::lock_guard<std::mutex> lock(mutex);
    accepting = false;
}

void DatabaseExecutor::Stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex);
        accepting = false;
        stopRequested = true;
        jobs.clear();
    }
    condition.notify_all();
    if (worker.joinable())
    {
        worker.join();
    }
}

std::size_t DatabaseExecutor::RunningJobCount() const
{
    return runningJobs.load();
}

void DatabaseExecutor::WorkerLoop()
{
    ChatServiceStatus status = ChatServiceStatus::Unavailable;
    try
    {
        status = service.Start();
    }
    catch (...)
    {
        status = ChatServiceStatus::Unavailable;
    }

    {
        std::lock_guard<std::mutex> lock(mutex);
        startSucceeded = status == ChatServiceStatus::Succeeded;
        startCompleted = true;
        accepting = startSucceeded && !stopRequested;
        if (!startSucceeded)
        {
            stopRequested = true;
        }
    }
    startCondition.notify_all();

    if (status == ChatServiceStatus::Succeeded)
    {
        for (;;)
        {
            Job job;
            {
                std::unique_lock<std::mutex> lock(mutex);
                condition.wait(lock, [this] { return stopRequested || !jobs.empty(); });
                if (stopRequested)
                {
                    break;
                }
                job = std::move(jobs.front());
                jobs.pop_front();
                runningJobs.fetch_add(1);
            }

            try
            {
                job(service);
            }
            catch (...)
            {
            }
            runningJobs.fetch_sub(1);
        }
    }

    try
    {
        service.Stop();
    }
    catch (...)
    {
    }
}
