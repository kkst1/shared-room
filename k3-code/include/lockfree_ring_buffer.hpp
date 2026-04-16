#pragma once

#include <atomic>
#include <cstddef>
#include <optional>
#include <vector>

// 单生产者单消费者（SPSC）无锁环形队列。
// 线程1->线程2、线程1->线程4 这种固定一进一出模型非常适合用 SPSC。
template <typename T>
class SpscRingBuffer {
public:
    explicit SpscRingBuffer(size_t capacity)
        : capacity_(capacity + 1), buffer_(capacity_) {}

    // 生产者入队。
    // 返回 false 表示队列已满，此时上层可选择丢弃或覆盖。
    bool push(T item) {
        const auto head = head_.load(std::memory_order_relaxed);
        const auto next = increment(head);
        if (next == tail_.load(std::memory_order_acquire)) {
            return false;
        }
        buffer_[head] = std::move(item);
        head_.store(next, std::memory_order_release);
        return true;
    }

    // 消费者出队。
    // 返回 std::nullopt 表示队列为空。
    std::optional<T> pop() {
        const auto tail = tail_.load(std::memory_order_relaxed);
        if (tail == head_.load(std::memory_order_acquire)) {
            return std::nullopt;
        }
        T item = std::move(buffer_[tail]);
        tail_.store(increment(tail), std::memory_order_release);
        return item;
    }

    // 只用于监控，不用于强一致业务判断。
    size_t approx_size() const {
        const auto head = head_.load(std::memory_order_acquire);
        const auto tail = tail_.load(std::memory_order_acquire);
        if (head >= tail) {
            return head - tail;
        }
        return capacity_ - (tail - head);
    }

private:
    size_t increment(size_t i) const { return (i + 1) % capacity_; }

    const size_t capacity_;
    std::vector<T> buffer_;
    std::atomic<size_t> head_ {0};
    std::atomic<size_t> tail_ {0};
};

