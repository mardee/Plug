#pragma once

#include "NightShiftTimeGrid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace glint
{
// UI-thread only, no JUCE/MIDI runtime dependencies. All timestamps use ONE
// monotonic millisecond clock. Mask/index order is the engine order, not display
// order: root, upper third/fifth/octave, lower third/fifth/octave.
class NightShiftLighting
{
public:
    using Grid = NightShiftTimeGrid;
    using Mask = std::uint8_t;
    using Revision = std::uint64_t;
    static constexpr std::size_t laneCount = 7, echoCapacity = 3;
    static constexpr double welcomeMs = 1500.0, enableMs = 700.0, echoMs = 850.0, echoSweepMs = 420.0;
    static constexpr double echoHoldMs = 160.0, idlePeriodMs = 4000.0, idleFlashMs = 550.0, lanePhaseMs = 45.0;
    static constexpr float steadyLevel = 0.15f, peakVelocity = 0.8f;

    // Values are named, discrete tiers, never additive brightness contributions.
    enum class Level : std::uint8_t { off = 0, base = 15, mark = 40, echo = 70, peak = 100 };
    static constexpr float levelValue (Level level) noexcept
    {
        switch (level)
        {
            case Level::base: return 0.15f;
            case Level::mark: return 0.4f;
            case Level::echo: return 0.7f;
            case Level::peak: return 1.0f;
            case Level::off: return 0.0f;
        }
        return 0.0f;
    }
    static Level velocityLevel (float velocity) noexcept
    {
        if (!std::isfinite (velocity) || velocity <= 0.0f) return Level::off;
        return velocity >= peakVelocity ? Level::peak : Level::echo;
    }

    // Progress is [0,1); -1 means inactive. Each real pulse retains its own
    // input-relative beat position/page: concurrent clouds have no unique clock.
    struct Pulse
    {
        float progress = -1.0f, strength = 0.0f;
        float sweepProgress = -1.0f; // The 420 ms head finishes before the actual-cell afterglow.
        int cell = -1, step = -1; // step is an alias value for cell, not absolute time.
        double onsetBeats = -1.0, beatPosition = -1.0, page = -1.0;
        Level level = Level::off;
    };
    struct Sample
    {
        bool enabled = false, echoActive = false;
        float steady = 0.0f, idle = 0.0f;
        float enableProgress = -1.0f, welcomeProgress = -1.0f, idleProgress = -1.0f;
        float flash = 0.0f, decorationGain = 1.0f;
        std::size_t echoCount = 0;
        std::array<Pulse, echoCapacity> echoes {};
    };
    struct CellLevels
    {
        Level edge = Level::off, center = Level::off;
        bool target = false, echoActualCell = false;
    };

    // Visibility session: repeated start is idempotent, stop/start welcomes again.
    // Initial OFF lanes join the full-field welcome only. Explicit OFF cancels
    // their welcome immediately; OFF never participates in idle/enable/echo.
    void start (double nowMs, Mask enabledMask, Revision revision) noexcept
    {
        if (!std::isfinite (nowMs)) return;
        if (started_) { syncEnabled (enabledMask, revision, nowMs); return; }
        started_ = true;
        establish (enabledMask, revision, nowMs);
        welcomeAt_ = nowMs;
        for (auto& lane : lanes_) lane.welcomeAllowed = true;
    }
    void stop() noexcept { *this = NightShiftLighting {}; }

    // The timer owns clock advancement. A backwards update is an explicit quiet
    // rebase: keep enables, discard all transients, reject older timestamps.
    // The caller also discards queued telemetry from the old clock/session; its
    // timestamps alone cannot identify events from an earlier clock epoch.
    // Queries and echo receipt NEVER advance/rebase this clock. syncEnabled also
    // advances it for legacy callers; using both with the same frame time is safe.
    void update (double nowMs) noexcept
    {
        if (!started_ || !std::isfinite (nowMs)) return;
        if (nowMs < frameNow_) establish (enabledMask(), revision_, nowMs);
        else frameNow_ = nowMs;
    }
    void advance (double nowMs) noexcept { update (nowMs); }

    // A changed revision is a whole-state restore, not an enable animation.
    // A same-revision transition clears that lane's old echoes and welcome.
    void syncEnabled (Mask mask, Revision revision, double nowMs) noexcept
    {
        if (!started_ || !std::isfinite (nowMs)) return;
        if (revision != revision_ || nowMs < frameNow_)
        {
            establish (mask, revision, nowMs);
            return;
        }
        update (nowMs);
        for (std::size_t i = 0; i < laneCount; ++i)
        {
            const bool enabled = (mask & (1u << i)) != 0;
            auto& lane = lanes_[i];
            if (enabled == lane.enabled) continue;
            lane = Lane {};
            lane.enabled = enabled;
            lane.idleAt = lane.acceptAfter = nowMs;
            if (enabled) lane.enableAt = nowMs;
        }
    }

    // Feed only successfully emitted, consumed real echo events. Velocity is
    // MIDI/127.f. Late events keep their age; expired/future/pre-lifecycle events
    // are rejected. The legacy overload has no known time cell: only edge sweep.
    bool echo (std::size_t lane, float velocity, double eventTimeMs, double nowMs) noexcept
    {
        return addEcho (lane, velocity, eventTimeMs, nowMs, -1.0);
    }
    bool echo (std::size_t lane, float velocity, double eventTimeMs, double nowMs, double onsetBeats) noexcept
    {
        if (Grid::cellForOnset (onsetBeats) < 0) return false;
        return addEcho (lane, velocity, eventTimeMs, nowMs, onsetBeats);
    }

    // Pure snapshots, including out-of-order timestamps from paint/timer. Reading
    // a future or older frame cannot expire events, restart welcome, or reset any
    // other lane. Call update/syncEnabled, NOT sample, to announce clock resets.
    Sample sample (std::size_t laneIndex, double nowMs) const noexcept
    {
        Sample out;
        if (!started_ || !std::isfinite (nowMs) || laneIndex >= laneCount) return out;
        const auto& lane = lanes_[laneIndex];
        out.enabled = lane.enabled;
        if (lane.welcomeAllowed) out.welcomeProgress = progress (nowMs, welcomeAt_, welcomeMs);
        if (!lane.enabled) return out;
        out.steady = steadyLevel;
        out.enableProgress = progress (nowMs, lane.enableAt, enableMs);
        for (const auto& event : lane.echoes)
        {
            const float p = progress (nowMs, event.at, echoMs);
            if (p < 0.0f) continue;
            auto& pulse = out.echoes[out.echoCount++];
            pulse.progress = p;
            pulse.sweepProgress = progress (nowMs, event.at, echoSweepMs);
            pulse.strength = event.strength;
            pulse.cell = pulse.step = Grid::cellForOnset (event.onsetBeats);
            pulse.onsetBeats = pulse.beatPosition = event.onsetBeats;
            pulse.page = Grid::pageForOnset (event.onsetBeats);
            pulse.level = velocityLevel (event.strength);
            if (nowMs - event.at < echoHoldMs) out.flash = std::max (out.flash, levelValue (pulse.level));
        }
        out.echoActive = out.echoCount != 0;
        out.decorationGain = out.echoActive ? 0.25f : 1.0f; // Legacy decoration hint only.
        const double idleAge = nowMs - lane.idleAt - idlePeriodMs - lanePhaseMs * laneIndex;
        if (!out.echoActive && idleAge >= 0.0)
        {
            const double phase = std::fmod (idleAge, idlePeriodMs);
            if (phase < idleFlashMs)
            {
                out.idleProgress = static_cast<float> (phase / idleFlashMs);
                out.idle = levelValue (Level::echo); // Head tier, NOT a whole-row flash.
            }
        }
        return out;
    }
    Sample sample (std::size_t laneIndex) const noexcept { return sample (laneIndex, frameNow_); }
    float welcomeProgress (double nowMs) const noexcept
    {
        return started_ && std::isfinite (nowMs) ? progress (nowMs, welcomeAt_, welcomeMs) : -1.0f;
    }
    float welcomeProgress() const noexcept { return welcomeProgress (frameNow_); }

    // Edge lights describe the grid/decoration. Center lights distinguish mere
    // parameter settings (mark) from a known actual echo cell (echo/peak + tail).
    // The decorative edge head is NOT a playhead or a prediction of future MIDI.
    // These remain discrete target tiers; the renderer smooths per-cell changes,
    // including welcome/enable/idle tails, without extending their trigger clocks.
    static CellLevels cellLevels (const Sample& state, int cell, bool target = false) noexcept
    {
        CellLevels out;
        if (!Grid::validCell (cell)) return out;
        out.target = target;
        out.edge = sweepLevel (state.welcomeProgress, cell, Level::peak);
        if (!state.enabled) return out;
        out.edge = brighter (out.edge, cell % Grid::stepsPerBeat == 0 ? Level::mark : Level::base);
        if (target) out.center = Level::mark;
        out.edge = brighter (out.edge, sweepLevel (state.enableProgress, cell, Level::peak));
        out.edge = brighter (out.edge, sweepLevel (state.idleProgress, cell, Level::echo));
        for (std::size_t i = 0; i < state.echoCount && i < echoCapacity; ++i)
        {
            const auto& pulse = state.echoes[i];
            if (pulse.progress < 0.0f || pulse.progress >= 1.0f) continue;
            out.edge = brighter (out.edge, sweepLevel (pulse.sweepProgress, cell, pulse.level));
            if (pulse.cell != cell) continue;
            out.echoActualCell = true;
            // Hold velocity classification for 160 ms, then step through the
            // lower tiers over a long afterglow; the renderer smooths each step.
            // The onset cell remains queryable after the faster edge sweep ends.
            const double age = pulse.progress * echoMs;
            const Level tail = age < echoHoldMs ? pulse.level
                : age < 500.0 && pulse.level == Level::peak ? Level::echo : Level::mark;
            out.center = brighter (out.center, tail);
        }
        return out;
    }
    static CellLevels cellLevels (const Sample& state, int cell, int firstChoice, int repeatChoice, double page = 0.0) noexcept
    {
        return cellLevels (state, cell, Grid::isTargetCell (firstChoice, repeatChoice, cell, page));
    }
    static Level cellLevel (const Sample& state, int cell, bool target = false) noexcept
    {
        const auto levels = cellLevels (state, cell, target);
        return brighter (levels.edge, levels.center);
    }
    float intensityAt (std::size_t laneIndex, float normalizedX, double nowMs) const noexcept
    {
        return intensityAt (sample (laneIndex, nowMs), normalizedX);
    }
    static float intensityAt (const Sample& state, float normalizedX) noexcept
    {
        const float x = std::isfinite (normalizedX) ? unit (normalizedX) : 0.0f;
        return levelValue (cellLevel (state, static_cast<int> (std::lround (x * Grid::stepsPerWindow))));
    }

private:
    static constexpr double inactive = -std::numeric_limits<double>::infinity();
    struct Event { double at = inactive; float strength = 0.0f; double onsetBeats = -1.0; };
    struct Lane
    {
        bool enabled = false, welcomeAllowed = false;
        double enableAt = inactive, idleAt = 0.0, acceptAfter = 0.0;
        std::array<Event, echoCapacity> echoes {};
    };
    bool started_ = false;
    Revision revision_ = 0;
    double welcomeAt_ = inactive, frameNow_ = 0.0;
    std::array<Lane, laneCount> lanes_ {};

    static float unit (float v) noexcept { return std::clamp (v, 0.0f, 1.0f); }
    static Level brighter (Level a, Level b) noexcept { return a > b ? a : b; }
    static float progress (double now, double at, double duration) noexcept
    {
        const double age = now - at;
        return age >= 0.0 && age < duration ? static_cast<float> (age / duration) : -1.0f;
    }
    static Level sweepLevel (float p, int cell, Level headLevel) noexcept
    {
        if (!std::isfinite (p) || p < 0.0f || p >= 1.0f) return Level::off;
        // One narrow group at a time; head visits both endpoints before its tail
        // exits. All seven lanes share this identical cell-coordinate function.
        const float distance = p * (Grid::stepsPerWindow + 8.0f) - cell;
        if (distance < -1.25f || distance > 8.0f) return Level::off;
        if (distance <= 1.5f) return headLevel;
        if (distance <= 4.5f) return headLevel == Level::peak ? Level::echo : Level::mark;
        return Level::mark;
    }
    Mask enabledMask() const noexcept
    {
        Mask mask = 0;
        for (std::size_t i = 0; i < laneCount; ++i)
            if (lanes_[i].enabled) mask = static_cast<Mask> (mask | (1u << i));
        return mask;
    }
    void establish (Mask mask, Revision revision, double now) noexcept
    {
        revision_ = revision;
        frameNow_ = now;
        welcomeAt_ = inactive;
        lanes_ = {};
        for (std::size_t i = 0; i < laneCount; ++i)
        {
            lanes_[i].enabled = (mask & (1u << i)) != 0;
            lanes_[i].idleAt = lanes_[i].acceptAfter = now;
        }
    }
    bool addEcho (std::size_t laneIndex, float velocity, double eventTimeMs, double nowMs, double onsetBeats) noexcept
    {
        if (!started_ || !std::isfinite (nowMs) || nowMs < frameNow_ || laneIndex >= laneCount
            || !std::isfinite (eventTimeMs) || !std::isfinite (velocity) || velocity <= 0.0f) return false;
        auto& lane = lanes_[laneIndex];
        if (!lane.enabled || eventTimeMs < lane.acceptAfter || eventTimeMs > nowMs
            || nowMs - eventTimeMs >= echoMs) return false;
        auto* slot = &lane.echoes[0];
        for (auto& candidate : lane.echoes)
            if (candidate.at < slot->at) slot = &candidate;
        if (slot->at > eventTimeMs) return false;
        *slot = { eventTimeMs, unit (velocity), onsetBeats };
        lane.idleAt = std::max (lane.idleAt, eventTimeMs + echoMs);
        return true;
    }
};
} // namespace glint
