#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace order_matching {

template <typename T, std::size_t Capacity>
class alignas(64) SpscRingBuffer {
    static_assert(Capacity > 0, "SpscRingBuffer capacity must be positive");
    static_assert(std::is_nothrow_move_constructible_v<T> ||
                      std::is_nothrow_copy_constructible_v<T>,
                  "Queue values must be safely transferable without throwing");

    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
    };

public:
    SpscRingBuffer() noexcept = default;
    SpscRingBuffer(const SpscRingBuffer&) = delete;
    SpscRingBuffer& operator=(const SpscRingBuffer&) = delete;

    ~SpscRingBuffer() noexcept {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        for (std::size_t index = head; index != tail; ++index) {
            object(slots_[index % Capacity])->~T();
        }
    }

    [[nodiscard]] bool try_push(const T& value) noexcept(
        std::is_nothrow_copy_constructible_v<T>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) {
            return false;
        }
        ::new (static_cast<void*>(slots_[tail % Capacity].storage)) T(value);
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_push(T&& value) noexcept(
        std::is_nothrow_move_constructible_v<T>) {
        const std::size_t tail = tail_.load(std::memory_order_relaxed);
        const std::size_t head = head_.load(std::memory_order_acquire);
        if (tail - head == Capacity) {
            return false;
        }
        ::new (static_cast<void*>(slots_[tail % Capacity].storage)) T(std::move(value));
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool try_pop(T& value) noexcept(
        std::is_nothrow_move_assignable_v<T> || std::is_nothrow_copy_assignable_v<T>) {
        const std::size_t head = head_.load(std::memory_order_relaxed);
        const std::size_t tail = tail_.load(std::memory_order_acquire);
        if (head == tail) {
            return false;
        }
        T* queued = object(slots_[head % Capacity]);
        value = std::move(*queued);
        queued->~T();
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    [[nodiscard]] bool empty() const noexcept {
        return head_.load(std::memory_order_acquire) ==
               tail_.load(std::memory_order_acquire);
    }

    [[nodiscard]] std::size_t size() const noexcept {
        return tail_.load(std::memory_order_acquire) -
               head_.load(std::memory_order_acquire);
    }

private:
    [[nodiscard]] static T* object(Slot& slot) noexcept {
        return std::launder(reinterpret_cast<T*>(slot.storage));
    }

    std::array<Slot, Capacity> slots_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};

} // namespace order_matching