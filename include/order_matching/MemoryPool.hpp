#pragma once

#include <array>
#include <cstddef>
#include <new>
#include <type_traits>
#include <utility>

namespace order_matching {

template <typename T, std::size_t Capacity>
class alignas(64) ObjectPool {
    static_assert(Capacity > 0, "ObjectPool capacity must be positive");

    struct Slot {
        alignas(T) std::byte storage[sizeof(T)];
        bool occupied{false};
    };

public:
    ObjectPool() noexcept {
        for (std::size_t index = 0; index < Capacity; ++index) {
            free_indices_[index] = Capacity - index - 1;
        }
    }

    ObjectPool(const ObjectPool&) = delete;
    ObjectPool& operator=(const ObjectPool&) = delete;

    ~ObjectPool() noexcept {
        for (Slot& slot : slots_) {
            if (slot.occupied) {
                object(slot).~T();
            }
        }
    }

    template <typename... Arguments>
    [[nodiscard]] T* acquire(Arguments&&... arguments) noexcept(
        std::is_nothrow_constructible_v<T, Arguments...>) {
        if (free_count_ == 0) {
            return nullptr;
        }
        const std::size_t index = free_indices_[--free_count_];
        Slot& slot = slots_[index];
        T* value = ::new (static_cast<void*>(slot.storage)) T(
            std::forward<Arguments>(arguments)...);
        slot.occupied = true;
        return value;
    }

    void release(T* value) noexcept {
        if (value == nullptr) {
            return;
        }
        const std::size_t index = index_of(value);
        Slot& slot = slots_[index];
        value->~T();
        slot.occupied = false;
        free_indices_[free_count_++] = index;
    }

    [[nodiscard]] std::size_t available() const noexcept { return free_count_; }
    [[nodiscard]] static constexpr std::size_t capacity() noexcept { return Capacity; }

private:
    [[nodiscard]] static T& object(Slot& slot) noexcept {
        return *std::launder(reinterpret_cast<T*>(slot.storage));
    }

    [[nodiscard]] std::size_t index_of(const T* value) const noexcept {
        const auto* bytes = reinterpret_cast<const std::byte*>(value);
        const auto* first = reinterpret_cast<const std::byte*>(slots_.data());
        return static_cast<std::size_t>(bytes - first) / sizeof(Slot);
    }

    std::array<Slot, Capacity> slots_{};
    std::array<std::size_t, Capacity> free_indices_{};
    std::size_t free_count_{Capacity};
};

} // namespace order_matching