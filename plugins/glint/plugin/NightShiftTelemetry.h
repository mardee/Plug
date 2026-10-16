#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace ozo::nightshift
{
struct LaneVisualEvent
{
    std::size_t laneIndex = 0; // Engine order, not the editor's displayed row order.
    std::uint64_t sampleTimestamp = 0; // Engine clock, resets at prepare.
    int sampleOffset = 0;
    int velocity = 0; // Exact generated MIDI velocity, 1..127.
    // juce::Time::getMillisecondCounterHiRes() at block entry + offset/sampleRate.
    // This may be in the future. It is a presentation estimate, not host/device latency.
    double presentationTimeMs = 0.0;
    std::uint64_t stateRevision = 0; // One revision covers prepare, reset and restore.
    double onsetBeats = 0.0;
    double repeatBeats = 0.0;
};

// Fixed-storage SPSC: one serialized audio producer, one message-thread consumer.
// Session/lifecycle methods belong ONLY to that consumer. Never reset the indices
// from prepare or a host state thread: release/acquire protects whole slot values.
class LaneVisualEventQueue
{
public:
    static constexpr std::size_t capacity = 256;
    static_assert (std::atomic<std::uint64_t>::is_always_lock_free,
                   "Visual telemetry requires lock-free 64-bit atomics");

    // Message thread. Rotating the session also rejects an in-flight old block
    // that publishes after the discard snapshot. Reopening never replays it.
    void beginSession() noexcept
    {
        activeSession_.store (0, std::memory_order_release);
        discardPending();
        if (++sessionSerial_ == 0)
            ++sessionSerial_; // Zero always means closed.
        consumerSession_ = sessionSerial_;
        activeSession_.store (consumerSession_, std::memory_order_release);
    }

    void endSession() noexcept
    {
        activeSession_.store (0, std::memory_order_release);
        consumerSession_ = 0;
        discardPending();
    }

    // Message thread baseline: discard queued AND in-flight pre-baseline events.
    void discard() noexcept
    {
        if (consumerSession_ != 0)
            beginSession();
        else
            discardPending();
    }

    // Audio thread: capture once at block entry and keep it with that block.
    std::uint64_t producerSession() const noexcept
    {
        return activeSession_.load (std::memory_order_acquire);
    }

    // Audio thread only. Full/closed/stale sessions drop VISUAL events only.
    // No allocation, locks, compare/exchange retry loops, or consumer-side calls.
    bool push (std::uint64_t session, const LaneVisualEvent& event) noexcept
    {
        if (session == 0 || session != producerSession())
            return false;
        const auto write = write_.load (std::memory_order_relaxed);
        if (write - read_.load (std::memory_order_acquire) >= capacity)
            return false;
        slots_[static_cast<std::size_t> (write % capacity)] = { event, session };
        write_.store (write + 1, std::memory_order_release);
        return true;
    }

    // Message thread only. Bounded work; skips old session/revision records.
    bool pop (LaneVisualEvent& event, std::uint64_t revision) noexcept
    {
        if (consumerSession_ == 0)
            return false;
        for (std::size_t checked = 0; checked < capacity; ++checked)
        {
            const auto read = read_.load (std::memory_order_relaxed);
            if (read == write_.load (std::memory_order_acquire))
                return false;
            const auto slot = slots_[static_cast<std::size_t> (read % capacity)];
            read_.store (read + 1, std::memory_order_release);
            if (slot.session == consumerSession_ && slot.event.stateRevision == revision)
            {
                event = slot.event;
                return true;
            }
        }
        return false;
    }

private:
    void discardPending() noexcept
    {
        read_.store (write_.load (std::memory_order_acquire), std::memory_order_release);
    }

    struct Slot { LaneVisualEvent event; std::uint64_t session = 0; };
    std::array<Slot, capacity> slots_ {};
    std::atomic<std::uint64_t> read_ { 0 }, write_ { 0 }, activeSession_ { 0 };
    std::uint64_t sessionSerial_ = 0, consumerSession_ = 0; // Consumer-owned only.
};
}
