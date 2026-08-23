#pragma once

#include <cstddef>
#include <cstdint>
#include <deque>
#include <mutex>
#include <unordered_set>
#include <utility>
#include <vector>

template <typename T>
class BoundedQueue
{
public:
    explicit BoundedQueue(std::size_t capacity)
        : capacity(capacity)
    {
    }

    bool TryPush(T value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (values.size() >= capacity)
        {
            return false;
        }

        values.push_back(std::move(value));
        return true;
    }

    bool TryPop(T& value)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (values.empty())
        {
            return false;
        }

        value = std::move(values.front());
        values.pop_front();
        return true;
    }

    std::vector<T> Drain()
    {
        std::lock_guard<std::mutex> lock(mutex);
        std::vector<T> drained;
        drained.reserve(values.size());
        while (!values.empty())
        {
            drained.push_back(std::move(values.front()));
            values.pop_front();
        }
        return drained;
    }

    void Clear()
    {
        std::lock_guard<std::mutex> lock(mutex);
        values.clear();
    }

    std::size_t Size() const
    {
        std::lock_guard<std::mutex> lock(mutex);
        return values.size();
    }

private:
    const std::size_t capacity;
    mutable std::mutex mutex;
    std::deque<T> values;
};

struct ByteView
{
    const std::uint8_t* data = nullptr;
    std::size_t size = 0;
};

class SerializedSendQueue
{
public:
    explicit SerializedSendQueue(std::size_t capacity)
        : capacity(capacity)
    {
    }

    bool TryPush(std::vector<std::uint8_t> bytes)
    {
        if (bytes.empty() || frames.size() >= capacity)
        {
            return false;
        }

        frames.push_back(std::move(bytes));
        return true;
    }

    ByteView Current() const
    {
        if (frames.empty())
        {
            return {};
        }

        const auto& frame = frames.front();
        return { frame.data() + offset, frame.size() - offset };
    }

    bool Consume(std::size_t bytes)
    {
        if (frames.empty() || bytes > frames.front().size() - offset)
        {
            return false;
        }

        offset += bytes;
        if (offset == frames.front().size())
        {
            frames.pop_front();
            offset = 0;
        }
        return true;
    }

    bool Empty() const
    {
        return frames.empty();
    }

    std::size_t Size() const
    {
        return frames.size();
    }

    void Clear()
    {
        frames.clear();
        offset = 0;
    }

private:
    const std::size_t capacity;
    std::deque<std::vector<std::uint8_t>> frames;
    std::size_t offset = 0;
};

class BoundedRequestTracker
{
public:
    explicit BoundedRequestTracker(std::size_t capacity)
        : capacity(capacity)
    {
    }

    bool TryTrack(std::uint32_t requestId)
    {
        if (requestId == 0 || requestIds.size() >= capacity)
        {
            return false;
        }
        return requestIds.insert(requestId).second;
    }

    bool Consume(std::uint32_t requestId)
    {
        return requestIds.erase(requestId) == 1;
    }

    std::size_t Size() const
    {
        return requestIds.size();
    }

private:
    const std::size_t capacity;
    std::unordered_set<std::uint32_t> requestIds;
};
