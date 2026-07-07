// FilterTableUS core — lock-free cross-thread exchange primitives. FROZEN after Phase 0.
//
// TripleBuffer<T>  : single-producer / single-consumer snapshot mailbox for trivially
//                    copyable T. write() never blocks and always succeeds (latest wins);
//                    read() is wait-free and returns false when nothing new arrived.
//
// ObjectHandoff<T> : publishes shared_ptr-owned immutable objects to a real-time consumer.
//                    publish()/collectGarbage() are message-thread-only (same thread as each
//                    other); acquire() is audio-thread-only, wait-free, and NEVER destroys
//                    anything. The GC keeps every object that is still visible as pending,
//                    current, or previous, so the audio thread can hold current+previous
//                    (e.g. across a crossfade) safely.
//
// NOTE: std::atomic<std::shared_ptr> is deliberately not used (unavailable in the deployment
// toolchain); lifetime is managed with raw-pointer atomics + a message-thread retention list.
#pragma once
#include <atomic>
#include <cstdint>
#include <memory>
#include <type_traits>
#include <vector>

namespace ftc {

template <class T>
class TripleBuffer {
    static_assert(std::is_trivially_copyable_v<T>, "TripleBuffer requires trivially copyable T");
public:
    TripleBuffer() = default;

    /// Producer side. Any single thread (may differ from consumer).
    void write(const T& value) noexcept {
        slots_[static_cast<size_t>(writeIdx_)] = value;
        const int prev = middle_.exchange(writeIdx_ | kFresh, std::memory_order_acq_rel);
        writeIdx_ = prev & kIdxMask;
    }

    /// Consumer side (single thread). Returns true and fills `out` when a new snapshot arrived.
    bool read(T& out) noexcept {
        if ((middle_.load(std::memory_order_acquire) & kFresh) == 0)
            return false;
        const int taken = middle_.exchange(readIdx_, std::memory_order_acq_rel);
        readIdx_ = taken & kIdxMask; // fresh by construction: only the producer stores fresh slots
        out = slots_[static_cast<size_t>(readIdx_)];
        return true;
    }

private:
    static constexpr int kFresh = 4;    // flag bit above the 2-bit slot index
    static constexpr int kIdxMask = 3;
    T slots_[3] = {};
    int writeIdx_ = 0;                  // producer-owned
    int readIdx_ = 2;                   // consumer-owned
    std::atomic<int> middle_{1};        // slot index (+ kFresh when unconsumed)
};

template <class T>
class ObjectHandoff {
public:
    /// Message thread only. Retains `obj` and makes it visible to acquire().
    void publish(std::shared_ptr<const T> obj) {
        const T* raw = obj.get();
        retained_.push_back(std::move(obj));
        pending_.store(raw, std::memory_order_release);
    }

    /// Audio thread only. Adopts the latest published object; returns the current one.
    /// Keeps the previously-current object visible to the GC (crossfade-safe).
    const T* acquire() noexcept {
        const T* p = pending_.load(std::memory_order_acquire);
        if (p != current_) {
            previous_ = current_;
            current_ = p;
            previousPub_.store(previous_, std::memory_order_release);
            currentPub_.store(current_, std::memory_order_release);
        }
        return current_;
    }

    /// Audio thread only: the object adopted by the last acquire() (no adoption side effect).
    const T* currentAcquired() const noexcept { return current_; }

    /// Message thread only. Frees retained objects no longer reachable as pending/current/previous.
    void collectGarbage() {
        const T* keep0 = pending_.load(std::memory_order_acquire);
        const T* keep1 = currentPub_.load(std::memory_order_acquire);
        const T* keep2 = previousPub_.load(std::memory_order_acquire);
        std::erase_if(retained_, [&](const std::shared_ptr<const T>& sp) {
            const T* r = sp.get();
            return r != keep0 && r != keep1 && r != keep2;
        });
    }

    /// Message thread: number of retained objects (for tests).
    std::size_t retainedCount() const noexcept { return retained_.size(); }

private:
    std::vector<std::shared_ptr<const T>> retained_; // message-thread-owned
    std::atomic<const T*> pending_{nullptr};
    std::atomic<const T*> currentPub_{nullptr};
    std::atomic<const T*> previousPub_{nullptr};
    const T* current_ = nullptr;   // audio-thread-owned
    const T* previous_ = nullptr;  // audio-thread-owned
};

} // namespace ftc
