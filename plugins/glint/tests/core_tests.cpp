// Standalone C++17 core tests. No JUCE, framework, assert(), or NDEBUG dependency.
// All timestamps below are absolute render samples, not host-transport samples.
#include "../core/ParticleEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <functional>
#include <iomanip>
#include <iostream>
#include <limits>
#include <new>
#include <sstream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#if defined(_MSC_VER)
#include <malloc.h>
#endif

// Only the allocation test enables this thread-local probe, and only during
// process(). Its fixtures and diagnostics never execute inside the probe.
// This intercepts C++17 new/delete (including arrays, sized, aligned, nothrow),
// not direct malloc/realloc or allocations on unrelated threads.
namespace AllocationAudit
{
thread_local bool enabled = false;
thread_local std::size_t allocations = 0;
thread_local std::size_t deallocations = 0;

void* allocate (std::size_t size, std::size_t alignment = 0)
{
    if (enabled)
        ++allocations;
    size = std::max<std::size_t> (size, 1);
    if (alignment != 0)
    {
        if (size > std::numeric_limits<std::size_t>::max() - (alignment - 1))
            throw std::bad_alloc();
        size = (size + alignment - 1) / alignment * alignment;
    }
    for (;;)
    {
       #if defined(_MSC_VER)
        void* memory = alignment == 0 ? std::malloc (size) : _aligned_malloc (size, alignment);
       #else
        void* memory = alignment == 0 ? std::malloc (size) : std::aligned_alloc (alignment, size);
       #endif
        if (memory != nullptr)
            return memory;
        const auto handler = std::get_new_handler();
        if (handler == nullptr)
            throw std::bad_alloc();
        handler();
    }
}

void release (void* memory, bool aligned = false) noexcept
{
    if (enabled && memory != nullptr)
        ++deallocations;
   #if defined(_MSC_VER)
    if (aligned)
        _aligned_free (memory);
    else
        std::free (memory);
   #else
    (void) aligned;
    std::free (memory);
   #endif
}

class Scope
{
public:
    Scope() noexcept
    {
        allocations = 0;
        deallocations = 0;
        enabled = true;
    }
    ~Scope() { enabled = false; }
    Scope (const Scope&) = delete;
    Scope& operator= (const Scope&) = delete;
};
}

void* operator new (std::size_t size) { return AllocationAudit::allocate (size); }
void* operator new[] (std::size_t size) { return AllocationAudit::allocate (size); }
void operator delete (void* memory) noexcept { AllocationAudit::release (memory); }
void operator delete[] (void* memory) noexcept { AllocationAudit::release (memory); }
void operator delete (void* memory, std::size_t) noexcept { AllocationAudit::release (memory); }
void operator delete[] (void* memory, std::size_t) noexcept { AllocationAudit::release (memory); }
void* operator new (std::size_t size, std::align_val_t alignment)
{
    return AllocationAudit::allocate (size, static_cast<std::size_t> (alignment));
}
void* operator new[] (std::size_t size, std::align_val_t alignment)
{
    return AllocationAudit::allocate (size, static_cast<std::size_t> (alignment));
}
void operator delete (void* memory, std::align_val_t) noexcept { AllocationAudit::release (memory, true); }
void operator delete[] (void* memory, std::align_val_t) noexcept { AllocationAudit::release (memory, true); }
void operator delete (void* memory, std::size_t, std::align_val_t) noexcept { AllocationAudit::release (memory, true); }
void operator delete[] (void* memory, std::size_t, std::align_val_t) noexcept { AllocationAudit::release (memory, true); }
void* operator new (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new (size); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, const std::nothrow_t&) noexcept
{
    try { return ::operator new[] (size); } catch (...) { return nullptr; }
}
void operator delete (void* memory, const std::nothrow_t&) noexcept { AllocationAudit::release (memory); }
void operator delete[] (void* memory, const std::nothrow_t&) noexcept { AllocationAudit::release (memory); }
void* operator new (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return ::operator new (size, alignment); } catch (...) { return nullptr; }
}
void* operator new[] (std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept
{
    try { return ::operator new[] (size, alignment); } catch (...) { return nullptr; }
}
void operator delete (void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    AllocationAudit::release (memory, true);
}
void operator delete[] (void* memory, std::align_val_t, const std::nothrow_t&) noexcept
{
    AllocationAudit::release (memory, true);
}

namespace
{
using namespace ozo::midi;
using Sample = std::int64_t;
using Bytes = std::vector<std::uint8_t>;
constexpr double sampleRate = 48000.0;
constexpr Sample second = 48000;
constexpr int voiceBudget = 32;
constexpr Sample isolatedSpacing = 40 * second;
constexpr Sample replyTail = 26 * second; // 24s hard lifetime plus at most a 1.5s gate.

static_assert (noexcept (std::declval<ParticleEngine&>().process (
                   1, std::declval<const Transport&>(), std::declval<const Controls&>(),
                   std::declval<MidiInput&>(), std::declval<MidiOutput&>())),
               "The real-time process API must be noexcept");
static_assert (std::is_same<typename std::decay<decltype (
                   std::declval<ParticleEngine&>().getStatus())>::type, EngineStatus>::value,
               "getStatus must expose EngineStatus");

class Failure : public std::runtime_error
{
public:
    using std::runtime_error::runtime_error;
};

void require (bool condition, const char* expression, const char* file, int line,
              const std::string& detail = {})
{
    if (condition)
        return;
    std::ostringstream message;
    message << file << ':' << line << ": " << expression;
    if (! detail.empty())
        message << " -- " << detail;
    throw Failure (message.str());
}

#define CHECK(condition) require (static_cast<bool> (condition), #condition, __FILE__, __LINE__)
#define CHECK_MSG(condition, detail) \
    require (static_cast<bool> (condition), #condition, __FILE__, __LINE__, (detail))

struct Event
{
    Sample sample = 0;
    Bytes bytes;
};
using Trace = std::vector<Event>;

bool sameEvent (const Event& a, const Event& b)
{
    return a.sample == b.sample && a.bytes == b.bytes;
}

std::string describe (const Event& event)
{
    std::ostringstream text;
    text << "sample=" << event.sample << " bytes=";
    for (auto byte : event.bytes)
        text << std::hex << std::setw (2) << std::setfill ('0') << static_cast<int> (byte) << ' ';
    return text.str();
}

void sortInput (Trace& events)
{
    // Never sort equal-time MIDI by status/pitch: delivered source order matters.
    std::stable_sort (events.begin(), events.end(), [] (const Event& a, const Event& b)
    {
        return a.sample < b.sample;
    });
}

Event note (Sample at, bool on, int pitch, int velocity = 100, int channel = 1)
{
    CHECK (at >= 0 && channel >= 1 && channel <= 16);
    CHECK (pitch >= 0 && pitch <= 127 && velocity >= 0 && velocity <= 127);
    return { at, { static_cast<std::uint8_t> ((on ? 0x90 : 0x80) | (channel - 1)),
                   static_cast<std::uint8_t> (pitch), static_cast<std::uint8_t> (velocity) } };
}

Event cc (Sample at, int controller, int value, int channel = 1)
{
    return { at, { static_cast<std::uint8_t> (0xb0 | (channel - 1)),
                   static_cast<std::uint8_t> (controller), static_cast<std::uint8_t> (value) } };
}

bool isOn (const Event& event)
{
    return event.bytes.size() == 3 && (event.bytes[0] & 0xf0) == 0x90 && event.bytes[2] != 0;
}

bool isOff (const Event& event)
{
    return event.bytes.size() == 3
        && ((event.bytes[0] & 0xf0) == 0x80
            || ((event.bytes[0] & 0xf0) == 0x90 && event.bytes[2] == 0));
}

int keyOf (const Event& event)
{
    return (event.bytes[0] & 0x0f) * 128 + event.bytes[1];
}

std::size_t onCount (const Trace& trace, Sample begin = 0,
                     Sample end = std::numeric_limits<Sample>::max())
{
    return static_cast<std::size_t> (std::count_if (trace.begin(), trace.end(),
        [=] (const Event& event) { return isOn (event) && event.sample >= begin && event.sample < end; }));
}

// A fresh iterator is supplied for every block. It borrows stable input bytes;
// offsets are calculated from the absolute sample, including exact block edges.
class BlockInput final : public MidiInput
{
public:
    BlockInput (const Trace& source, std::size_t begin, std::size_t end, Sample blockStart)
        : source_ (source), index_ (begin), end_ (end), start_ (blockStart) {}

    bool next (MidiEventView& view) noexcept override
    {
        ++calls;
        if (index_ == end_)
            return false;
        const auto& event = source_[index_++];
        view.sampleOffset = static_cast<int> (event.sample - start_);
        view.data = event.bytes.data();
        view.size = static_cast<int> (event.bytes.size());
        return true;
    }

    std::size_t position() const { return index_; }
    int calls = 0;

private:
    const Trace& source_;
    std::size_t index_, end_;
    Sample start_;
};

class BlockOutput final : public MidiOutput
{
public:
    BlockOutput (Trace& result, Sample start, int length)
        : result_ (result), start_ (start), length_ (length) {}

    void push (int offset, const std::uint8_t* data, int size) noexcept override
    {
        // Record failures rather than throwing through the noexcept core callback.
        if (offset < 0 || offset >= length_)
        {
            invalidOffset = true;
            return;
        }
        if (data == nullptr || size <= 0)
        {
            invalidBytes = true;
            return;
        }
        if (offset < lastOffset_)
            outOfOrder = true;
        lastOffset_ = offset;
        if (result_.size() >= 1000000)
        {
            excessiveOutput = true;
            return;
        }
        // The output interface explicitly requires a copy before returning.
        result_.push_back ({ start_ + offset, Bytes (data, data + size) });
    }

    void check() const
    {
        CHECK_MSG (! invalidOffset, "output offset must lie in [0, blockLength)");
        CHECK_MSG (! invalidBytes, "output must contain a non-null, non-empty byte message");
        CHECK_MSG (! outOfOrder, "core output must already be in nondecreasing sample order");
        CHECK_MSG (! excessiveOutput, "render exceeded one million MIDI events");
    }

private:
    Trace& result_;
    Sample start_;
    int length_, lastOffset_ = -1;
    bool invalidOffset = false, invalidBytes = false, outOfOrder = false, excessiveOutput = false;
};

struct StatusAt
{
    Sample sample;
    EngineStatus value;
};

class Render
{
public:
    explicit Render (Trace events = {}, Controls initial = {}, double rate = sampleRate)
        : input (std::move (events)), controls (initial), rate_ (rate)
    {
        sortInput (input);
        for (const auto& event : input)
            CHECK (event.sample >= 0 && ! event.bytes.empty());
        engine.prepare (rate_);
    }

    Transport transport() const
    {
        Transport result;
        result.bpm = bpm_;
        result.hasTempo = hasTempo;
        result.hasPpq = hasPpq;
        result.hasSamplePosition = hasSamplePosition;
        result.isPlaying = playing_;
        const Sample delta = playing_ ? now - anchorWall_ : 0;
        result.samplePosition = anchorSample_ + delta;
        const double tempoForPosition = std::isfinite (bpm_) && bpm_ > 0.0 ? bpm_ : 120.0;
        // Derive position from an anchor, not repeatedly accumulated block deltas.
        result.ppqPosition = anchorPpq_ + static_cast<double> (delta) * tempoForPosition / (60.0 * rate_);
        return result;
    }

    void setPlaying (bool playing)
    {
        const auto current = transport();
        anchorSample_ = current.samplePosition;
        anchorPpq_ = current.ppqPosition;
        anchorWall_ = now;
        playing_ = playing;
    }

    void seek (Sample hostSample, double hostPpq)
    {
        anchorWall_ = now;
        anchorSample_ = hostSample;
        anchorPpq_ = hostPpq;
    }

    void setTempo (double bpm)
    {
        const auto current = transport();
        anchorSample_ = current.samplePosition;
        anchorPpq_ = current.ppqPosition;
        anchorWall_ = now;
        bpm_ = bpm;
    }

    void enqueue (Event event)
    {
        CHECK (event.sample >= now);
        const auto position = std::upper_bound (input.begin(), input.end(), event.sample,
            [] (Sample at, const Event& existing) { return at < existing.sample; });
        input.insert (position, std::move (event));
    }

    EngineStatus status() { return engine.getStatus(); }

    void zeroLength (int nextBlockLength = 960)
    {
        const auto end = endFor (nextBlockLength);
        BlockInput source (input, cursor_, end, now);
        BlockOutput sink (output, now, 0);
        const auto beforeSize = output.size();
        const auto clock = transport();
        engine.process (0, clock, controls, source, sink);
        sink.check();
        CHECK_MSG (source.calls == 0 && source.position() == cursor_,
                   "zero-length processing must not even advance the MIDI iterator");
        CHECK (output.size() == beforeSize);
        checkStatus();
    }

    void step (int frames = 960)
    {
        CHECK (frames > 0);
        if (insertZeroBlocks)
            zeroLength (frames);
        const auto end = endFor (frames);
        BlockInput source (input, cursor_, end, now);
        BlockOutput sink (output, now, frames);
        const auto clock = transport();
        engine.process (frames, clock, controls, source, sink);
        sink.check();
        CHECK_MSG (source.position() == end, "a positive-length block must consume all its input events");
        cursor_ = end;
        now += frames;
        checkStatus();
        statuses.push_back ({ now, status() });
    }

    void until (Sample end, int blockSize = 960)
    {
        CHECK (end >= now && blockSize > 0);
        while (now < end)
            step (static_cast<int> (std::min<Sample> (blockSize, end - now)));
    }

    void forSeconds (double seconds, int blockSize = 960)
    {
        until (now + static_cast<Sample> (std::llround (seconds * rate_)), blockSize);
    }

    void waitForGeneratedKey()
    {
        const Sample deadline = now + static_cast<Sample> (10.0 * rate_);
        while (now < deadline && status().generatedKeysDown == 0)
            step (32);
        CHECK_MSG (status().generatedKeysDown > 0, "fixture must actually reach a live generated note");
    }

    Trace consumedInput() const
    {
        return Trace (input.begin(), input.begin() + static_cast<std::ptrdiff_t> (cursor_));
    }

    ParticleEngine engine;
    Trace input, output;
    Controls controls;
    Sample now = 0;
    std::vector<StatusAt> statuses;
    bool hasTempo = true, hasPpq = true, hasSamplePosition = true;
    bool insertZeroBlocks = false;

private:
    std::size_t endFor (int frames) const
    {
        auto end = cursor_;
        while (end < input.size() && input[end].sample < now + frames)
        {
            CHECK (input[end].sample >= now);
            ++end;
        }
        return end;
    }

    void checkStatus()
    {
        const auto state = status();
        CHECK (state.generatedKeysDown >= 0 && state.generatedHeld >= 0);
        CHECK_MSG (state.generatedKeysDown + state.generatedHeld <= voiceBudget,
                   "keys plus pedal-held generated occupancy exceeded the public 32-voice budget");
    }

    double rate_, bpm_ = 120.0;
    Sample anchorWall_ = 0, anchorSample_ = 0;
    double anchorPpq_ = 0.0;
    bool playing_ = true;
    std::size_t cursor_ = 0;
};

// Retired product contracts: Character, Direction, Rate, OctaveRange, Rhythm,
// SpecificEcho and the old 77-case count belong to the archived multi-purpose core.
// This suite tests only the single-purpose finite-reply contract, not those modes.
Controls settings (OutputMode mode = OutputMode::particles, float amount = 1.0f)
{
    Controls result;
    result.outputMode = mode;
    result.amount = amount;
    return result;
}

void sameTrace (const Trace& expected, const Trace& actual, const std::string& context)
{
    CHECK_MSG (expected.size() == actual.size(), context + ": expected "
        + std::to_string (expected.size()) + " events, got " + std::to_string (actual.size()));
    for (std::size_t i = 0; i < expected.size(); ++i)
    {
        // No unadvertised rounding tolerance: compare the full ordered MIDI trace exactly.
        CHECK_MSG (sameEvent (expected[i], actual[i]), context + ": event " + std::to_string (i)
            + " expected " + describe (expected[i]) + ", got " + describe (actual[i]));
    }
}

Trace generatedInAdd (const Trace& output, const Trace& input)
{
    Trace generated;
    std::size_t sourceIndex = 0;
    for (const auto& event : output)
    {
        if (sourceIndex < input.size() && sameEvent (event, input[sourceIndex]))
            ++sourceIndex;
        else
            generated.push_back (event);
    }
    CHECK_MSG (sourceIndex == input.size(), "Add must preserve every original byte, sample, and equal-time order");
    return generated;
}

void balancedGenerated (const Trace& trace)
{
    std::array<int, 16 * 128> down {};
    int total = 0;
    Sample previous = -1;
    for (const auto& event : trace)
    {
        CHECK (event.sample >= previous);
        previous = event.sample;
        CHECK_MSG (isOn (event) || isOff (event),
                   "generated output may not invent CC/pedal/panic messages: " + describe (event));
        CHECK (event.bytes[1] < 128 && event.bytes[2] < 128);
        auto& key = down[static_cast<std::size_t> (keyOf (event))];
        if (isOn (event))
        {
            CHECK_MSG (key == 0, "overlapping generated notes on the same MIDI key: " + describe (event));
            ++key;
            ++total;
            CHECK (total <= voiceBudget);
        }
        else
        {
            CHECK_MSG (key == 1, "unmatched or stale generated Note Off: " + describe (event));
            --key;
            --total;
        }
    }
    CHECK_MSG (total == 0, "every generated Note On needs a final Note Off");
}

void validOctaves (const Trace& generated, const Trace& source)
{
    std::array<std::array<bool, 12>, 16> pitchClasses {};
    for (const auto& event : source)
        if (isOn (event))
            pitchClasses[event.bytes[0] & 15][event.bytes[1] % 12] = true;
    for (const auto& event : generated)
    {
        CHECK (isOn (event) || isOff (event));
        CHECK (event.bytes[1] <= 127 && event.bytes[2] <= 127);
        if (isOn (event))
        {
            CHECK (event.bytes[2] >= 1);
            CHECK_MSG (pitchClasses[event.bytes[0] & 15][event.bytes[1] % 12],
                       "generated pitch/channel is not a source pitch plus integer octaves: " + describe (event));
        }
    }
}

void boundedRate (const Trace& generated, Sample samplesPerSecond = second)
{
    std::vector<Sample> times;
    for (const auto& event : generated)
        if (isOn (event))
            times.push_back (event.sample);
    std::size_t left = 0;
    for (std::size_t right = 0; right < times.size(); ++right)
    {
        while (times[right] - times[left] >= samplesPerSecond)
            ++left;
        CHECK_MSG (right - left + 1 <= 48, "generated Note On rate exceeded 48 in a half-open one-second window");
    }
}

Trace mixedMidi()
{
    Trace events {
        cc (0, 1, 73), note (0, true, 60, 110), note (31, false, 60, 17),
        { 32, { 0xe0, 0x00, 0x40 } }, { 63, { 0xc3, 29 } },
        { 64, { 0xd3, 87 } }, { 127, { 0xa3, 64, 97 } },
        cc (128, 64, 127), note (511, true, 72, 127, 16),
        note (512, true, 72, 0, 16), cc (959, 66, 127),
        note (960, true, 48, 90, 2), note (960, false, 48, 42, 2),
        note (960, true, 48, 91, 2), cc (960, 74, 68, 2),
        { 1023, { 0xe1, 0x7f, 0x7f } }, note (1024, false, 48, 0, 2),
        { 2048, { 0xf0, 0x7d, 0x01, 0x00, 0x7f, 0xf7 } },
        cc (2500, 64, 0), cc (2501, 66, 0), cc (3000, 123, 0),
        cc (3001, 120, 0), cc (3002, 121, 0), { 4096, { 0xf8 } }
    };
    Bytes sysex (513, 0);
    sysex.front() = 0xf0;
    sysex.back() = 0xf7;
    for (std::size_t i = 1; i + 1 < sysex.size(); ++i)
        sysex[i] = static_cast<std::uint8_t> (i % 128);
    events.push_back ({ 4097, std::move (sysex) });
    // Exercise harness sorting without disturbing the deliberately ordered ties.
    std::rotate (events.begin(), events.begin() + 4, events.end());
    sortInput (events);
    return events;
}

Trace musicalInput()
{
    Trace result;
    for (int strike = 0; strike < 12; ++strike)
    {
        const Sample at = strike * (second / 2) + (strike % 3) * 37;
        const int channel = strike % 3 + 1;
        const int root = 48 + (strike % 5) * 2;
        for (int interval : { 0, 4, 7 })
        {
            result.push_back (note (at, true, root + interval, 85 + strike % 35, channel));
            result.push_back (note (at + 4000 + strike * 13, false, root + interval, 0, channel));
        }
    }
    result.push_back (note (217, true, 0, 110, 15));
    result.push_back (note (8317, false, 0, 0, 15));
    result.push_back (note (419, true, 127, 115, 16));
    result.push_back (note (8519, false, 127, 0, 16));
    sortInput (result);
    return result;
}

void boundedGates (const Trace& generated, Sample samplesPerSecond = second)
{
    std::array<Sample, 16 * 128> starts;
    starts.fill (-1);
    for (const auto& event : generated)
    {
        CHECK (isOn (event) || isOff (event));
        auto& start = starts[static_cast<std::size_t> (keyOf (event))];
        if (isOn (event))
        {
            CHECK (start < 0);
            start = event.sample;
        }
        else
        {
            CHECK (start >= 0);
            CHECK_MSG (event.sample > start && event.sample - start <= samplesPerSecond * 3 / 2,
                       "generated gate must be positive and at most 1.5s: " + describe (event));
            start = -1;
        }
    }
    CHECK (std::all_of (starts.begin(), starts.end(), [] (Sample at) { return at < 0; }));
}

void testAddZero()
{
    Render render (mixedMidi(), settings (OutputMode::add, 0.0f));
    render.until (2 * second, 960);
    sameTrace (render.input, render.output, "Add Amount=0 byte passthrough");
    CHECK (render.status().generatedNoteOns == 0);
    CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
}

void testParticlesZero()
{
    Render render (mixedMidi(), settings (OutputMode::particles, 0.0f));
    render.until (20 * second);
    CHECK (render.output.empty());
    CHECK (render.status().activeMode == OutputMode::particles);
    CHECK (render.status().generatedNoteOns == 0);
}

void testDenseLifecycle()
{
    Trace input;
    for (int strike = 0; strike < 1200; ++strike)
    {
        const Sample at = strike * 200;
        const int pitch = 12 + strike % 100;
        const int channel = 1 + strike % 16;
        input.push_back (note (at, true, pitch, 1 + strike % 127, channel));
        input.push_back (note (at + 5000, false, pitch, 0, channel));
    }
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        Render render (input, settings (mode));
        render.until (30 * second, 128);
        const auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
        CHECK (onCount (generated) > 0);
        balancedGenerated (generated);
        validOctaves (generated, render.input);
        boundedRate (generated);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    }
}

void testAmountInterrupt()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        Render render ({ note (0, true, 60, 110) }, settings (mode));
        render.waitForGeneratedKey();
        const auto cutoff = render.now;
        render.controls.amount = 0.0f;
        render.step (32);
        CHECK (render.status().generatedKeysDown == 0);
        render.forSeconds (20.0);
        // Re-enabling alone must not resurrect canceled candidates or held sources.
        render.controls.amount = 1.0f;
        render.forSeconds (10.0);
        const auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
        CHECK (onCount (generated, cutoff) == 0);
        balancedGenerated (generated);
    }
}

void testBypass()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        Render render ({ note (0, true, 60, 110) }, settings (mode));
        render.waitForGeneratedKey();
        const auto cutoff = render.now;
        render.controls.bypass = true;
        render.enqueue (cc (cutoff, 1, 72));
        render.enqueue ({ cutoff + 1, { 0xe0, 1, 65 } });
        render.enqueue ({ cutoff + 2, { 0xf0, 0x7d, 0x0a, 0xf7 } });
        render.enqueue (note (cutoff + 3, true, 67, 120));
        render.enqueue (note (cutoff + 4, false, 67, 0));
        render.step (32);
        CHECK (render.status().generatedKeysDown == 0);
        render.forSeconds (15.0);
        render.controls.bypass = false;
        render.forSeconds (10.0);
        auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.consumedInput()) : render.output;
        CHECK (onCount (generated, cutoff) == 0);
        balancedGenerated (generated);
        const auto resume = render.now;
        render.enqueue (note (resume, true, 69, 115));
        render.enqueue (note (resume + second / 8, false, 69, 0));
        render.forSeconds (20.0);
        generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
        CHECK (onCount (generated, resume) > 0);
        balancedGenerated (generated);
    }
}

void testStopLive (bool sameBlock)
{
    Render render ({ note (0, true, 60, 110) }, settings());
    render.waitForGeneratedKey();
    const auto stop = render.now;
    render.setPlaying (false);
    if (! sameBlock)
    {
        render.step (32);
        CHECK (render.status().generatedKeysDown == 0);
        render.forSeconds (2.0);
        CHECK (onCount (render.output, stop) == 0);
    }
    const auto live = render.now;
    const auto frozen = render.transport();
    render.enqueue (note (live, true, 67, 115));
    render.enqueue (note (live + second / 6, false, 67, 0));
    render.forSeconds (25.0, 128);
    CHECK (render.transport().ppqPosition == frozen.ppqPosition);
    CHECK (render.transport().samplePosition == frozen.samplePosition);
    CHECK_MSG (onCount (render.output, live) > 0, "stopped callbacks must advance fresh live generation and note endings");
    balancedGenerated (render.output);
    CHECK (render.status().generatedKeysDown == 0);
}

void testDiscontinuity (bool loop)
{
    Render render ({ note (0, true, 60, 110) }, settings());
    render.waitForGeneratedKey();
    const auto jump = render.now;
    if (loop)
        render.seek (0, 0.0);
    else
        render.seek (20 * second, 40.0);
    render.step (32);
    CHECK (render.status().generatedKeysDown == 0);
    render.forSeconds (20.0);
    CHECK (onCount (render.output, jump) == 0);
    balancedGenerated (render.output);
    const auto fresh = render.now;
    render.enqueue (note (fresh, true, 69, 110));
    render.enqueue (note (fresh + second / 8, false, 69, 0));
    render.forSeconds (20.0);
    CHECK (onCount (render.output, fresh) > 0);
    balancedGenerated (render.output);
}

void testVelocityZero()
{
    Trace ordinary { note (0, true, 60, 105), note (1234, false, 60, 0),
                     note (second, true, 67, 111), note (second + 2345, false, 67, 0) };
    auto zeroVelocity = ordinary;
    zeroVelocity[1].bytes[0] = 0x90;
    zeroVelocity[3].bytes[0] = 0x90;
    Render off (ordinary, settings());
    Render zero (zeroVelocity, settings());
    off.until (25 * second, 128);
    zero.until (25 * second, 128);
    CHECK (onCount (off.output) > 0);
    sameTrace (off.output, zero.output, "velocity zero must be semantically identical to Note Off");
    balancedGenerated (zero.output);

    Render repeated ({ note (0, true, 60, 100), note (1, true, 60, 110) },
                     settings (OutputMode::add, 0.0f));
    repeated.step (32);
    repeated.controls.outputMode = OutputMode::particles;
    repeated.enqueue (note (repeated.now, true, 60, 0));
    repeated.step (32);
    CHECK (repeated.status().activeMode == OutputMode::add && repeated.status().modeChangePending);
    repeated.enqueue (note (repeated.now, false, 60, 0));
    repeated.step (32);
    repeated.step (32);
    CHECK (repeated.status().activeMode == OutputMode::particles && ! repeated.status().modeChangePending);
}

void testCollisionNoStaleOff()
{
    const Trace source { note (0, true, 60, 110), note (32, false, 60, 0) };
    const auto control = settings (OutputMode::add);
    Render baseline (source, control);
    baseline.until (25 * second, 64);
    const auto generated = generatedInAdd (baseline.output, baseline.input);
    balancedGenerated (generated);
    Event chosenOn, chosenOff;
    bool found = false;
    for (std::size_t i = 0; i < generated.size() && ! found; ++i)
    {
        if (! isOn (generated[i]) || generated[i].sample <= 32)
            continue;
        for (std::size_t j = i + 1; j < generated.size(); ++j)
            if (isOff (generated[j]) && keyOf (generated[j]) == keyOf (generated[i]))
            {
                if (generated[j].sample - generated[i].sample >= 4)
                {
                    chosenOn = generated[i];
                    chosenOff = generated[j];
                    found = true;
                }
                break;
            }
    }
    CHECK_MSG (found, "derive collision timing from a real generated On/Off pair, not a hard-coded gate");
    const Sample retrigger = chosenOn.sample + (chosenOff.sample - chosenOn.sample) / 2;
    const Sample release = chosenOff.sample + 3 * second;
    const int channel = (chosenOn.bytes[0] & 15) + 1;
    const int pitch = chosenOn.bytes[1];
    auto input = source;
    const auto originalOn = note (retrigger, true, pitch, 127, channel);
    input.push_back (originalOn);
    input.push_back (note (release, false, pitch, 0, channel));
    Render collision (input, control);
    collision.until (release + 25 * second, 64);
    const auto residual = generatedInAdd (collision.output, collision.input);
    balancedGenerated (residual);
    bool sawOriginal = false, sawCleanupBeforeOriginal = false;
    for (const auto& event : collision.output)
    {
        if (event.sample == retrigger && isOff (event) && keyOf (event) == keyOf (chosenOn) && ! sawOriginal)
            sawCleanupBeforeOriginal = true;
        if (sameEvent (event, originalOn))
        {
            sawOriginal = true;
            CHECK_MSG (sawCleanupBeforeOriginal, "particle Off must be inserted before the same-sample original retrigger");
        }
        CHECK_MSG (! (isOff (event) && keyOf (event) == keyOf (chosenOn)
                      && event.sample > retrigger && event.sample < release),
                   "stale particle Off would terminate the held original retrigger: " + describe (event));
    }
    CHECK (sawOriginal && sawCleanupBeforeOriginal);

    // A raw source Off can own the release of a sounding generated MIDI key.
    // It must pass through once and retire that voice's later scheduled Off.
    auto offInput = source;
    const auto rawOff = note (retrigger, false, pitch, 23, channel);
    offInput.push_back (rawOff);
    Render raw (offInput, control);
    raw.until (retrigger, 64);
    raw.step (1);
    raw.controls.amount = 0.0f;
    raw.forSeconds (5.0, 64);
    generatedInAdd (raw.output, raw.input); // byte/sample/order preservation
    int ownedOffs = 0;
    for (const auto& event : raw.output)
    {
        if (event.sample == retrigger && sameEvent (event, rawOff)) ++ownedOffs;
        CHECK_MSG (! (isOff (event) && keyOf (event) == keyOf (chosenOn)
                      && event.sample > retrigger), "raw Off must retire a particle's scheduled Off");
    }
    CHECK (ownedOffs == 1);
    Trace effective;
    for (const auto& event : raw.output)
        if (event.sample > 32) effective.push_back (event);
    balancedGenerated (effective);
}

void testSustainBudget()
{
    Trace source { cc (0, 64, 127) };
    for (int strike = 0; strike < 500; ++strike)
    {
        const Sample at = strike * 4000 + 1;
        const int pitch = 24 + strike % 72;
        source.push_back (note (at, true, pitch, 115));
        source.push_back (note (at + 1500, false, pitch, 0));
    }
    Render render (source, settings (OutputMode::add));
    render.until (45 * second, 128);
    int maxOccupied = 0;
    for (const auto& snapshot : render.statuses)
        maxOccupied = std::max (maxOccupied, snapshot.value.generatedKeysDown + snapshot.value.generatedHeld);
    CHECK (maxOccupied > 0 && maxOccupied <= voiceBudget);
    CHECK_MSG (render.status().generatedHeld > 0, "shared sustain must retain generated occupancy after Note Off");
    render.controls.amount = 0.0f;
    render.step (32);
    CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld > 0);
    render.enqueue (cc (render.now, 64, 0));
    render.step (32);
    CHECK (render.status().generatedHeld == 0);
    const auto resumed = render.now;
    render.controls.amount = 1.0f;
    render.enqueue (note (resumed, true, 72, 110));
    render.enqueue (note (resumed + second / 6, false, 72, 0));
    render.forSeconds (25.0);
    const auto generated = generatedInAdd (render.output, render.input);
    CHECK (onCount (generated, resumed) > 0);
    balancedGenerated (generated);
    boundedRate (generated);
}

void testSostenutoSnapshot()
{
    // CC66 held before any note must not capture notes played afterward.
    Render late ({ cc (0, 66, 127), note (1, true, 60, 110) }, settings (OutputMode::add));
    late.waitForGeneratedKey();
    late.controls.amount = 0.0f;
    late.step (32);
    CHECK (late.status().generatedKeysDown == 0 && late.status().generatedHeld == 0);
    balancedGenerated (generatedInAdd (late.output, late.consumedInput()));

    // Conversely, an already sounding generated key belongs to the CC66 snapshot.
    Render captured ({ note (0, true, 60, 110) }, settings (OutputMode::add));
    captured.waitForGeneratedKey();
    captured.enqueue (cc (captured.now, 66, 127));
    captured.step (1);
    captured.controls.amount = 0.0f;
    captured.step (32);
    CHECK (captured.status().generatedKeysDown == 0 && captured.status().generatedHeld > 0);
    captured.enqueue (cc (captured.now, 66, 0));
    captured.step (32);
    CHECK (captured.status().generatedHeld == 0);
    balancedGenerated (generatedInAdd (captured.output, captured.input));
}

void testPedalIntersection()
{
    for (bool sustainFirst : { false, true })
    {
        Render render ({ note (0, true, 60, 110) }, settings (OutputMode::add));
        render.waitForGeneratedKey();
        render.enqueue (cc (render.now, 66, 127));
        render.enqueue (cc (render.now, 64, 127));
        render.step (1);
        render.controls.amount = 0.0f;
        render.step (32);
        CHECK (render.status().generatedHeld > 0);
        render.enqueue (cc (render.now, sustainFirst ? 64 : 66, 0));
        render.step (32);
        CHECK_MSG (render.status().generatedHeld > 0, "releasing one pedal must not discard occupancy held by the other");
        render.enqueue (cc (render.now, sustainFirst ? 66 : 64, 0));
        render.step (32);
        CHECK (render.status().generatedHeld == 0);
        balancedGenerated (generatedInAdd (render.output, render.input));
    }
}

void testAllNotesOff()
{
    Render render ({ cc (0, 64, 127), note (1, true, 60, 110), note (32, false, 60, 0) },
                   settings (OutputMode::add));
    render.waitForGeneratedKey();
    render.enqueue (cc (render.now, 123, 0));
    render.step (1);
    CHECK (render.status().generatedKeysDown == 0);
    CHECK_MSG (render.status().generatedHeld > 0, "CC123 is a key release, not a sustain-independent sound kill");
    render.controls.amount = 0.0f;
    render.controls.outputMode = OutputMode::particles;
    render.step (32);
    CHECK (render.status().activeMode == OutputMode::add && render.status().modeChangePending);
    // Check original byte preservation; do not infer external audio silence from Off/CC123.
    const auto generated = generatedInAdd (render.output, render.consumedInput());
    validOctaves (generated, render.consumedInput());
    balancedGenerated (generated);
    render.enqueue (cc (render.now, 64, 0));
    render.step (32);
    render.step (32);
    CHECK (render.status().generatedHeld == 0);
    CHECK (render.status().activeMode == OutputMode::particles && ! render.status().modeChangePending);
}

void testAllSoundOff()
{
    Render render ({ cc (0, 64, 127), note (1, true, 60, 110), note (32, false, 60, 0) },
                   settings (OutputMode::add));
    render.waitForGeneratedKey();
    const auto kill = render.now;
    render.enqueue (cc (kill, 120, 0));
    render.step (1);
    CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    render.forSeconds (20.0);
    const auto generated = generatedInAdd (render.output, render.consumedInput());
    CHECK (onCount (generated, kill) == 0);
    validOctaves (generated, render.consumedInput());
    balancedGenerated (generated);
    render.controls.outputMode = OutputMode::particles;
    render.step (32);
    CHECK_MSG (render.status().activeMode == OutputMode::add && render.status().modeChangePending,
               "CC120 must not silently reset the still-down sustain controller");
    render.enqueue (cc (render.now, 64, 0));
    render.step (32);
    render.step (32);
    CHECK (render.status().activeMode == OutputMode::particles && ! render.status().modeChangePending);
}

void testResetControllers()
{
    Render held ({ cc (0, 64, 127), note (1, true, 60, 110), note (32, false, 60, 0) },
                 settings (OutputMode::add));
    held.waitForGeneratedKey();
    held.controls.amount = 0.0f;
    held.step (32);
    CHECK (held.status().generatedHeld > 0);
    held.enqueue (cc (held.now, 121, 0));
    held.step (32);
    CHECK (held.status().generatedKeysDown == 0 && held.status().generatedHeld == 0);
    balancedGenerated (generatedInAdd (held.output, held.input));

    // A public mode transition reveals whether CC121 incorrectly erased physical keys.
    Render physical ({ cc (0, 64, 127), note (1, true, 60, 110) },
                     settings (OutputMode::add, 0.0f));
    physical.step (32);
    physical.controls.outputMode = OutputMode::particles;
    physical.enqueue (cc (physical.now, 121, 0));
    physical.step (32);
    physical.step (32);
    CHECK_MSG (physical.status().activeMode == OutputMode::add && physical.status().modeChangePending,
               "CC121 must not clear a physically held source key");
    physical.enqueue (note (physical.now, false, 60, 0));
    physical.step (32);
    physical.step (32);
    CHECK (physical.status().activeMode == OutputMode::particles && ! physical.status().modeChangePending);
}

void testParticlesChannelMessages()
{
    for (int controller : { 120, 123 })
    {
        Render render ({ note (0, true, 60, 110), note (32, false, 60, 0) }, settings());
        render.waitForGeneratedKey();
        const auto command = render.now;
        render.enqueue (cc (command, controller, 0));
        render.step (1);
        CHECK (render.status().generatedKeysDown == 0);
        render.forSeconds (25.0);
        CHECK (onCount (render.output, command + 15 * second) == 0);
        // Particles suppresses channel controls, so its generated notes need explicit Offs.
        balancedGenerated (render.output);
    }
}

void testModePending()
{
    for (auto initial : { OutputMode::add, OutputMode::particles })
    {
        const auto target = initial == OutputMode::add ? OutputMode::particles : OutputMode::add;
        Render render ({ note (0, true, 60, 110) }, settings (initial, 0.0f));
        render.step (32);
        CHECK (render.status().activeMode == initial);
        render.controls.outputMode = target;
        render.step (32);
        CHECK (render.status().activeMode == initial && render.status().modeChangePending);
        render.enqueue (cc (render.now, 64, 127));
        render.enqueue (note (render.now + 1, false, 60, 0));
        render.step (32);
        render.step (32);
        CHECK_MSG (render.status().activeMode == initial && render.status().modeChangePending,
                   "a down pedal must defer output-mode changes after the physical key is released");
        render.enqueue (cc (render.now, 64, 0));
        render.step (32);
        render.step (32);
        CHECK (render.status().activeMode == target && ! render.status().modeChangePending);
        const auto marker = render.output.size();
        const auto probe = cc (render.now, 74, 91);
        render.enqueue (probe);
        render.step (32);
        if (target == OutputMode::add)
        {
            CHECK (render.output.size() == marker + 1);
            CHECK (sameEvent (render.output.back(), probe));
        }
        else
            CHECK (render.output.size() == marker);
    }
}

void testHotStateLoad()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        Render render ({ note (0, true, 60, 110) }, settings (mode));
        render.waitForGeneratedKey();
        const auto reset = render.now;
        ++render.controls.resetSerial;
        render.controls.seed ^= 0xa51c09edu;
        render.step (32);
        CHECK (render.status().generatedKeysDown == 0);
        render.forSeconds (20.0);
        auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.consumedInput()) : render.output;
        CHECK (onCount (generated, reset) == 0);
        balancedGenerated (generated);
        const auto fresh = render.now;
        render.enqueue (note (fresh, true, 67, 115));
        render.enqueue (note (fresh + second / 8, false, 67, 0));
        render.forSeconds (20.0);
        generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
        CHECK (onCount (generated, fresh) > 0);
        balancedGenerated (generated);
    }

    Render pedal ({ cc (0, 64, 127), note (1, true, 60, 110) }, settings (OutputMode::add));
    pedal.waitForGeneratedKey();
    ++pedal.controls.resetSerial;
    pedal.step (32);
    CHECK (pedal.status().generatedKeysDown == 0 && pedal.status().generatedHeld > 0);
    pedal.controls.amount = 0.0f;
    pedal.enqueue (cc (pedal.now, 64, 0));
    pedal.step (32);
    CHECK (pedal.status().generatedHeld == 0);
    balancedGenerated (generatedInAdd (pedal.output, pedal.input));
}

void testAbnormalTempo()
{
    const std::array<double, 10> tempos {{ 0.0, -120.0,
        std::numeric_limits<double>::quiet_NaN(), std::numeric_limits<double>::infinity(),
        -std::numeric_limits<double>::infinity(), std::numeric_limits<double>::denorm_min(),
        std::numeric_limits<double>::max(), 0.000001, 1.0, 1000000.0 }};
    for (double bpm : tempos)
    {
        Render render ({ note (0, true, 60, 110), note (second / 8, false, 60, 0) }, settings());
        render.hasPpq = false;
        render.setTempo (bpm);
        render.until (60 * second, 1024);
        balancedGenerated (render.output);
        boundedRate (render.output);
        CHECK_MSG (onCount (render.output, 24 * second) == 0,
                   "abnormal tempo must not stretch seed lifetime past 24 wall-clock seconds");
        boundedGates (render.output);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    }

    Render changing ({ note (0, true, 60, 110) }, settings());
    changing.waitForGeneratedKey();
    changing.setTempo (300.0);
    changing.forSeconds (1.0, 32);
    changing.setTempo (0.000001);
    changing.forSeconds (30.0, 1024);
    changing.setTempo (std::numeric_limits<double>::quiet_NaN());
    changing.hasPpq = false;
    changing.forSeconds (30.0, 128);
    CHECK (onCount (changing.output, changing.now - 20 * second) == 0);
    balancedGenerated (changing.output);
    boundedRate (changing.output);
}

void testSampleRatesAndMissingClock()
{
    for (double rate : { 44100.0, 48000.0, 96000.0 })
    {
        const auto unit = static_cast<Sample> (rate);
        Render render ({ note (0, true, 60, 110), note (unit / 8, false, 60, 0) }, settings(), rate);
        render.hasTempo = false;
        render.hasPpq = false;
        render.hasSamplePosition = false;
        render.setPlaying (false);
        render.until (30 * unit, 512);
        CHECK (onCount (render.output) > 0);
        CHECK (onCount (render.output, 20 * unit) == 0);
        balancedGenerated (render.output);
        boundedRate (render.output, unit);
    }
}

struct FixedMessage
{
    int offset = 0, size = 0;
    std::array<std::uint8_t, 513> bytes {};
};

class FixedInput final : public MidiInput
{
public:
    void add (int offset, std::initializer_list<std::uint8_t> bytes)
    {
        CHECK (count < messages.size());
        auto& event = messages[count++];
        event.offset = offset;
        event.size = static_cast<int> (bytes.size());
        std::copy (bytes.begin(), bytes.end(), event.bytes.begin());
    }

    bool next (MidiEventView& view) noexcept override
    {
        if (cursor == count)
            return false;
        const auto& event = messages[cursor++];
        view = { event.offset, event.bytes.data(), event.size };
        return true;
    }

    std::array<FixedMessage, 128> messages {};
    std::size_t count = 0, cursor = 0;
};

class FixedOutput final : public MidiOutput
{
public:
    void push (int offset, const std::uint8_t* data, int size) noexcept override
    {
        if (offset < 0 || offset >= frames || offset < previous || data == nullptr
            || size <= 0 || size > 513 || count >= messages.size())
        {
            valid = false;
            return;
        }
        auto& event = messages[count++];
        event.offset = offset;
        event.size = size;
        std::copy_n (data, size, event.bytes.begin());
        previous = offset;
    }

    std::array<FixedMessage, 256> messages {};
    std::size_t count = 0;
    int frames = 960, previous = -1;
    bool valid = true;
};

class FixedNoteObserver final : public GeneratedNoteObserver
{
public:
    explicit FixedNoteObserver (const FixedOutput& output) noexcept : output_ (output) {}

    void generatedNoteOn (const GeneratedNoteOn& event) noexcept override
    {
        if (count >= events.size())
        {
            valid = false;
            return;
        }
        events[count++] = event;
        ++total;
        if (event.laneIndex >= laneCount || event.velocity < 1 || event.velocity > 127
            || event.sampleOffset < 0 || event.sampleOffset >= output_.frames
            || event.sampleTimestamp != blockStart + static_cast<std::uint64_t> (event.sampleOffset)
            || output_.count == 0)
        {
            valid = false;
            return;
        }
        // Observation must occur AFTER the actual Note On was copied to output.
        const auto& emitted = output_.messages[output_.count - 1];
        valid = valid && emitted.size == 3 && (emitted.bytes[0] & 0xf0) == 0x90
            && emitted.bytes[2] == event.velocity && emitted.offset == event.sampleOffset;
    }

    std::array<GeneratedNoteOn, 256> events {};
    std::size_t count = 0;
    std::uint64_t blockStart = 0, total = 0;
    bool valid = true;

private:
    const FixedOutput& output_;
};

void testGeneratedObservation()
{
    constexpr std::array<int, laneCount> pitches {{ 74, 77, 81, 86, 71, 67, 62 }};
    for (bool manual : { false, true })
        for (auto mode : { OutputMode::add, OutputMode::particles })
        {
            ParticleEngine observed, baseline;
            observed.prepare (sampleRate);
            baseline.prepare (sampleRate);
            FixedInput input;
            FixedOutput output, reference;
            output.frames = reference.frames = 257;
            FixedNoteObserver observer (output);
            auto controls = settings (mode);
            controls.scale = Scale::major;
            controls.manualLanes = manual;
            for (auto& lane : controls.lanes)
                lane = { true, 4, 6, .8f, .4f, 0, .4f, 2 };
            std::array<bool, laneCount> seen {};
            Transport transport;
            // Deliberately exceed uint32 before a source: the timestamp is the
            // engine render clock, not a truncated counter or host position.
            for (int empty = 0; empty < 3; ++empty)
            {
                observed.process (std::numeric_limits<int>::max(), transport, controls, input, output);
                baseline.process (std::numeric_limits<int>::max(), transport, controls, input, reference);
                observer.blockStart += static_cast<std::uint64_t> (std::numeric_limits<int>::max());
            }
            for (int block = 0; block < 2000; ++block)
            {
                input.count = input.cursor = output.count = reference.count = observer.count = 0;
                output.previous = reference.previous = -1;
                if (block == 0) input.add (117, { 0x90, 74, 100 });
                if (block == 1) input.add (100, { 0x80, 74, 0 });
                observed.process (0, transport, controls, input, output, &observer);
                CHECK (observer.count == 0 && input.cursor == 0);
                observed.process (output.frames, transport, controls, input, output, &observer);
                input.cursor = 0;
                baseline.process (reference.frames, transport, controls, input, reference);
                CHECK (observer.valid && output.valid && reference.valid);
                CHECK (output.count == reference.count);
                std::size_t generated = 0;
                for (std::size_t index = 0; index < output.count; ++index)
                {
                    const auto& event = output.messages[index];
                    const auto& expected = reference.messages[index];
                    CHECK (event.offset == expected.offset && event.size == expected.size);
                    CHECK (std::equal (event.bytes.begin(), event.bytes.begin() + event.size,
                                       expected.bytes.begin()));
                    if (event.size != 3 || (event.bytes[0] & 0xf0) != 0x90 || event.bytes[2] == 0
                        || (mode == OutputMode::add && block == 0 && event.offset == 117))
                        continue;
                    CHECK (generated < observer.count);
                    const auto& visual = observer.events[generated++];
                    CHECK (event.bytes[1] == pitches[visual.laneIndex]);
                    CHECK (event.bytes[2] == visual.velocity && event.offset == visual.sampleOffset);
                    seen[visual.laneIndex] = true;
                }
                CHECK (generated == observer.count);
                CHECK (observed.getStatus().generatedNoteOns == observer.total);
                CHECK (observed.getStatus().generatedNoteOns == baseline.getStatus().generatedNoteOns);
                CHECK (observed.getStatus().skippedCandidates == baseline.getStatus().skippedCandidates);
                observer.blockStart += static_cast<std::uint64_t> (output.frames);
            }
            CHECK (observer.total > 0);
            CHECK (std::all_of (seen.begin(), seen.end(), [] (bool value) { return value; }));
        }
}

void testObservationNoFalseEvents()
{
    // Disabled, bypass, zero Amount, no scale, out-of-range pitch and occupied
    // unison all admit source/Off/CC traffic without inventing a visual Note On.
    for (int variant = 0; variant < 7; ++variant)
    {
        ParticleEngine engine;
        engine.prepare (sampleRate);
        FixedInput input;
        FixedOutput output;
        FixedNoteObserver observer (output);
        auto controls = settings (OutputMode::add, .30f);
        controls.manualLanes = true;
        for (auto& lane : controls.lanes) lane.enabled = false;
        const std::size_t lane = variant == 3 ? 1 : variant == 5 ? 6 : variant == 6 ? 0 : 3;
        controls.lanes[lane] = { variant != 0, 0, 2, .8f, .4f, 0, .0f, 2 };
        controls.bypass = variant == 1;
        if (variant == 2) controls.amount = 0.0f;
        const auto pitch = static_cast<std::uint8_t> (variant == 4 ? 127 : variant == 5 ? 40 : 60);
        for (int block = 0; block < 100; ++block)
        {
            input.count = input.cursor = output.count = observer.count = 0;
            output.previous = -1;
            if (block == 0)
            {
                input.add (117, { 0x90, pitch, 100 });
                input.add (118, { 0xb0, 1, 64 });
                input.add (119, { 0x90, 61, 0 }); // Velocity zero is an Off.
                if (variant != 6) input.add (357, { 0x80, pitch, 0 });
            }
            engine.process (output.frames, {}, controls, input, output, &observer);
            CHECK (observer.valid && observer.count == 0 && output.valid);
            observer.blockStart += static_cast<std::uint64_t> (output.frames);
        }
        CHECK (engine.getStatus().generatedNoteOns == 0);
        if (variant >= 3) CHECK (engine.getStatus().skippedCandidates > 0);
    }
}

void testProcessNoHeap()
{
    // Explicit calls prevent compiler new-expression allocation elision from
    // making the probe self-check spuriously pass without exercising replacements.
    {
        AllocationAudit::Scope probe;
        void* scalar = ::operator new (7);
        void* array = ::operator new[] (9, std::nothrow);
        void* aligned = ::operator new (64, std::align_val_t (64));
        void* alignedArray = ::operator new[] (64, std::align_val_t (64), std::nothrow);
        ::operator delete (scalar, std::size_t (7));
        ::operator delete[] (array, std::nothrow);
        ::operator delete (aligned, std::size_t (64), std::align_val_t (64));
        ::operator delete[] (alignedArray, std::align_val_t (64), std::nothrow);
    }
    CHECK (AllocationAudit::allocations == 4 && AllocationAudit::deallocations == 4);

    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        ParticleEngine engine;
        engine.prepare (sampleRate);
        FixedInput source;
        FixedOutput sink;
        FixedNoteObserver observer (sink);
        auto controls = settings (mode);
        Transport transport;
        transport.hasTempo = transport.hasPpq = transport.hasSamplePosition = true;
        transport.isPlaying = true;
        bool sawGeneration = false;
        std::size_t messages = 0;
        for (int block = 0; block < 1500; ++block)
        {
            source.count = source.cursor = 0;
            sink.count = 0;
            sink.previous = -1;
            sink.valid = true;
            transport.samplePosition = static_cast<Sample> (block) * sink.frames;
            transport.ppqPosition = static_cast<double> (transport.samplePosition) / 24000.0;
            observer.count = 0;
            observer.blockStart = static_cast<std::uint64_t> (transport.samplePosition);
            if (block == 0)
                source.add (0, { 0xb0, 64, 127 });
            if (block == 300)
                source.add (0, { 0xb0, 64, 0 });
            if (block == 320 || block == 400)
                source.add (0, { 0xb0, 66, static_cast<std::uint8_t> (block == 320 ? 127 : 0) });
            if (block == 450 || block == 500 || block == 550)
                source.add (0, { 0xb0, static_cast<std::uint8_t> (block == 450 ? 123 : block == 500 ? 121 : 120), 0 });
            if (block < 600)
            {
                for (int tone = 0; tone < 12; ++tone)
                    source.add (31, { 0x90, static_cast<std::uint8_t> (36 + (block + tone) % 60), 110 });
                source.add (511, { 0xe0, 0, 64 });
                source.add (512, { 0xf0, 0x7d, 1, 2, 3, 0xf7 });
                for (int tone = 0; tone < 12; ++tone)
                    source.add (959, { 0x80, static_cast<std::uint8_t> (36 + (block + tone) % 60), 0 });
            }
            // Dense 20ms packets can legitimately replace all three per-channel
            // seeds before their first reply. Sparse packets make the audit also
            // exercise real generation and its scheduled Offs, not just admission.
            if (block >= 750 && block <= 1000 && block % 50 == 0)
            {
                source.add (31, { 0x90, 60, 100 });
                source.add (959, { 0x80, 60, 0 });
            }
            controls.amount = block >= 200 && block < 210 ? 0.0f : 1.0f;
            controls.bypass = block >= 250 && block < 260;
            controls.root = (block / 100) % 12;
            controls.scale = block < 200 ? Scale::unset : block < 400 ? Scale::major : Scale::minor;
            if (block == 275)
                ++controls.resetSerial;
            if (block == 700)
                controls.outputMode = mode == OutputMode::add ? OutputMode::particles : OutputMode::add;

            {
                AllocationAudit::Scope probe;
                engine.process (sink.frames, transport, controls, source, sink, &observer);
            }
            const auto allocated = AllocationAudit::allocations;
            const auto freed = AllocationAudit::deallocations;
            CHECK_MSG (allocated == 0 && freed == 0, "process heap operations in block "
                       + std::to_string (block) + ": new=" + std::to_string (allocated)
                       + ", delete=" + std::to_string (freed));
            CHECK (sink.valid && source.cursor == source.count && observer.valid);
            CHECK (observer.total == engine.getStatus().generatedNoteOns);
            // At most 48 new Ons, their Offs, and 32 previously reserved Offs.
            CHECK (sink.count <= source.count + 2 * 48 + voiceBudget);
            messages += sink.count;
            const auto state = engine.getStatus();
            CHECK (state.generatedKeysDown + state.generatedHeld <= voiceBudget);
            sawGeneration = sawGeneration || state.generatedNoteOns != 0;
        }
        CHECK (messages > 0 && sawGeneration);
        CHECK (engine.getStatus().generatedKeysDown == 0 && engine.getStatus().generatedHeld == 0);
    }
}

void safety (const Trace& trace)
{
    balancedGenerated (trace);
    boundedRate (trace);
    boundedGates (trace);
}

Trace packets (int pitch, int strikes = 40, int velocity = 100)
{
    Trace result;
    for (int i = 0; i < strikes; ++i)
    {
        const Sample at = i * isolatedSpacing;
        result.push_back (note (at, true, pitch, velocity));
        result.push_back (note (at + 240, false, pitch, 0));
    }
    return result;
}

Trace partitionTrace (int block, OutputMode mode, std::uint32_t seed, bool zeros)
{
    auto control = settings (mode);
    control.seed = seed;
    Render render (musicalInput(), control);
    render.insertZeroBlocks = zeros;
    render.until (2 * second + 117, block);
    render.controls.amount = 0.6f;
    render.until (4 * second + 29, block);
    render.setTempo (173.0);
    render.until (16 * second, block);
    const auto generated = mode == OutputMode::add
        ? generatedInAdd (render.output, render.input) : render.output;
    CHECK (onCount (generated) > 0);
    safety (generated);
    return render.output;
}

void testDeterminismPartitionsAndZero()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        const auto reference = partitionTrace (32, mode, 19u, false);
        for (int block : { 64, 128, 512, 1024 })
            sameTrace (reference, partitionTrace (block, mode, 19u, false),
                       "exact block 32 versus " + std::to_string (block)
                       + " mode=" + std::to_string (static_cast<int> (mode))
                       + " including tempo/amount automation");
        sameTrace (reference, partitionTrace (128, mode, 19u, true), "zero blocks are inert");
    }
    for (std::uint32_t seed : { 0u, 0xffffffffu })
        sameTrace (partitionTrace (128, OutputMode::particles, seed, false),
                   partitionTrace (128, OutputMode::particles, seed, false), "seed repeatability");
}

void testIdle()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        Render render ({}, settings (mode));
        render.until (15 * second);
        CHECK (render.output.empty());
        CHECK (render.status().generatedNoteOns == 0);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    }
}

void testAllPitchBoundaries()
{
    for (int pitch = 0; pitch < 128; ++pitch)
        for (int velocity : { 1, 100, 127 })
        {
            Render render (packets (pitch, 1, velocity), settings());
            render.until (6 * second);
            CHECK (onCount (render.output) > 0);
            for (const auto& event : render.output)
                if (isOn (event))
                    CHECK (event.bytes[2] >= 1 && event.bytes[2] <= std::max (1, velocity * 48 / 100));
            validOctaves (render.output, render.input);
            safety (render.output);
        }
}

void testShortNoteFiniteReply()
{
    for (std::uint32_t seed : { 1u, 19u, 0x6f7a6f31u })
    {
        auto control = settings();
        control.seed = seed;
        Render render ({ note (117, true, 60), note (118, false, 60, 0) }, control);
        render.until (117 + replyTail, 2048);
        CHECK_MSG (onCount (render.output, 119) > 0, "one-sample source retains reply after release");
        CHECK_MSG (onCount (render.output, 117 + 16 * second) == 0,
                   "at 120 BPM the 32-beat hard lifetime is stricter than the 24-second cap");
        safety (render.output);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    }
}

void testChordDensity()
{
    std::array<std::size_t, 2> counts {};
    for (std::uint32_t seed : { 1u, 19u, 73u })
        for (int variant = 0; variant < 2; ++variant)
        {
            auto control = settings();
            control.seed = seed;
            Trace input;
            for (int strike = 0; strike < 16; ++strike)
                for (int tone = 0; tone < (variant == 0 ? 1 : 6); ++tone)
                {
                    const Sample at = strike * 4 * second;
                    input.push_back (note (at, true, 48 + tone * 2));
                    input.push_back (note (at + 240, false, 48 + tone * 2, 0));
                }
            Render render (input, control);
            render.until (68 * second);
            CHECK (onCount (render.output) > 0);
            counts[variant] += onCount (render.output);
            safety (render.output);
        }
    CHECK_MSG (counts[1] <= counts[0] * 2, "six simultaneous notes must not become six schedulers");
}

void testMelodyPreservesOlderReplies()
{
    bool heardOlderAfterNew = false;
    for (std::uint32_t seed : { 1u, 19u, 73u, 101u })
    {
        auto control = settings();
        control.seed = seed;
        Render render ({ note (0, true, 60), note (240, false, 60, 0),
                         note (second / 4, true, 62), note (second / 4 + 240, false, 62, 0),
                         note (second / 2, true, 64), note (second / 2 + 240, false, 64, 0) }, control);
        render.until (8 * second);
        for (const auto& event : render.output)
            if (isOn (event) && event.sample > second / 2 + 1680 && event.bytes[1] % 12 == 0)
                heardOlderAfterNew = true;
        CHECK (onCount (render.output, second / 2 + 4 * second + 1) == 0);
        safety (render.output);
    }
    CHECK_MSG (heardOlderAfterNew, "new melody packets must not immediately erase all older replies");
}

void testVelocityEnvelope()
{
    int rises = 0, falls = 0, earlyPeakSum = 0, latePeakSum = 0;
    std::array<bool, 128> velocities {};
    for (std::uint32_t seed : { 1u, 19u, 73u, 101u })
    {
        auto control = settings();
        control.seed = seed;
        Render render (packets (60), control);
        render.until (39 * isolatedSpacing + replyTail, 2048);
        safety (render.output);
        for (int packet = 0; packet < 40; ++packet)
        {
            std::vector<int> values;
            for (const auto& event : render.output)
                if (isOn (event) && event.sample >= packet * isolatedSpacing
                    && event.sample < (packet + 1) * isolatedSpacing)
                {
                    CHECK_MSG (event.bytes[2] >= 1 && event.bytes[2] <= 48, "src100 reply exceeds .48 ceiling");
                    values.push_back (event.bytes[2]);
                    velocities[event.bytes[2]] = true;
                }
            CHECK (values.size() >= 2);
            for (std::size_t i = 1; i < values.size(); ++i)
            {
                rises += values[i] > values[i - 1];
                falls += values[i] < values[i - 1];
            }
            const auto middle = values.begin() + static_cast<std::ptrdiff_t> ((values.size() + 1) / 2);
            earlyPeakSum += *std::max_element (values.begin(), middle);
            latePeakSum += *std::max_element (middle, values.end());
        }
    }
    CHECK_MSG (rises > 10 && falls > 10, "local rises and decay are required, not a monotonic velocity table");
    CHECK (std::count (velocities.begin(), velocities.end(), true) >= 8);
    CHECK_MSG (latePeakSum < earlyPeakSum, "aggregate late reply peaks must be lower than early peaks");
}

void testTimingVarietyAndRests()
{
    std::vector<std::vector<Sample>> shapes;
    std::vector<Sample> intervals;
    int rests = 0, shortGaps = 0;
    auto control = settings (OutputMode::particles, 1.0f);
    control.seed = 19u;
    control.scale = Scale::major;
    Render render (packets (74, 64), control);
    render.until (63 * isolatedSpacing + replyTail, 2048);
    for (int packet = 0; packet < 64; ++packet)
    {
        std::vector<Sample> times;
        for (const auto& event : render.output)
            if (isOn (event) && event.sample >= packet * isolatedSpacing && event.sample < (packet + 1) * isolatedSpacing)
                times.push_back (event.sample - packet * isolatedSpacing);
        CHECK (times.size() >= 2);
        shapes.push_back (times);
        for (std::size_t i = 1; i < times.size(); ++i)
        {
            CHECK_MSG (times[i] - times[i - 1] >= 960, "global replies must remain at least 20ms apart");
            shortGaps += times[i] - times[i - 1] <= 9001;
        }
        // Independent lanes may interleave densely; burst/rest applies to each
        // fixed-pitch lane, not the merged seven-voice stream.
        for (int pitch : { 74, 77, 81, 86, 71, 67, 62 })
        {
            std::vector<Sample> laneTimes;
            for (const auto& event : render.output)
                if (isOn (event) && event.bytes[1] == pitch
                    && event.sample >= packet * isolatedSpacing
                    && event.sample < (packet + 1) * isolatedSpacing)
                    laneTimes.push_back (event.sample);
            int shortRun = 0;
            for (std::size_t i = 1; i < laneTimes.size(); ++i)
            {
                const Sample gap = laneTimes[i] - laneTimes[i - 1];
                if (gap <= 9001)
                {
                    ++shortRun; ++shortGaps;
                    CHECK_MSG (shortRun <= 2, "a lane's short burst must contain at most three notes");
                }
                else
                {
                    if (shortRun >= 2)
                        CHECK_MSG (gap >= 11999, "a lane's short burst needs half-beat rest");
                    shortRun = 0;
                    rests += gap >= 11999;
                }
                intervals.push_back ((gap + 240) / 480);
            }
        }
    }
    std::sort (shapes.begin(), shapes.end());
    shapes.erase (std::unique (shapes.begin(), shapes.end()), shapes.end());
    std::sort (intervals.begin(), intervals.end());
    intervals.erase (std::unique (intervals.begin(), intervals.end()), intervals.end());
    CHECK_MSG (shapes.size() >= 8 && intervals.size() >= 3, "new packets need independent timing, not a fixed template");
    CHECK (rests > 0 && shortGaps > 0);
    safety (render.output);
    Render repeat (render.input, control);
    repeat.until (63 * isolatedSpacing + replyTail, 2048);
    sameTrace (render.output, repeat.output, "varied timing remains repeatable for a fixed seed");
}

// Only the isolated D4 / C-major fixtures use this mapping. Group by observable
// pitch class, not scheduler internals: third=F, fifth=A, octave=D.
int dInCMajorLane (int pitch)
{
    switch (pitch % 12)
    {
        case 5: return 0;
        case 9: return 1;
        case 2: return 2;
        default: CHECK_MSG (false, "D4 in C major may reply only with F/A/D pitch classes");
    }
    return -1;
}

Sample median (std::vector<Sample> values)
{
    CHECK (! values.empty());
    std::sort (values.begin(), values.end());
    return values[values.size() / 2];
}

void testFixedLanesAndDynamics()
{
    constexpr int packetCount = 32;
    constexpr int sourceVelocity = 100;
    constexpr Sample sourceOffset = 117;
    constexpr Sample spacing = isolatedSpacing;
    constexpr Sample beat = second / 2;
    // Exact D5 roles: up third/fifth/octave, down third/fifth/octave, unison.
    // PC alone cannot distinguish D4/D5/D6 in the seven-role response.
    const std::array<int, 7> pitches {{ 77, 81, 86, 71, 67, 62, 74 }};
    std::array<int, 7> rises {}, falls {}, earlyPeakSum {}, latePeakSum {}, velocitySum {}, counts {};
    std::array<std::vector<Sample>, 7> gates;
    std::array<std::vector<std::vector<Sample>>, 7> seedGapShapes;
    std::array<std::vector<Sample>, 7> allGaps;
    for (std::uint32_t seed : { 1u, 19u, 73u, 101u })
    {
        auto control = settings (OutputMode::add, 1.0f); // all seven roles; short source releases unison
        control.seed = seed;
        control.root = 0;
        control.scale = Scale::major;
        Trace input;
        for (int packet = 0; packet < packetCount; ++packet)
        {
            const Sample at = sourceOffset + packet * spacing;
            input.push_back (note (at, true, 74, sourceVelocity));
            input.push_back (note (at + 240, false, 74, 0));
        }
        Render render (input, control);
        render.until ((packetCount - 1) * spacing + sourceOffset + replyTail, 2048);
        const auto generated = generatedInAdd (render.output, render.input);
        safety (generated);
        std::array<std::array<Trace, 7>, packetCount> lanes;
        std::array<std::vector<Sample>, 7> seedGaps;
        std::array<Sample, 128> starts;
        starts.fill (-1);
        for (const auto& event : generated)
        {
            const auto role = std::find (pitches.begin(), pitches.end(), event.bytes[1]);
            CHECK_MSG (role != pitches.end(), "D5 seven-role response must use exact diatonic/octave targets: "
                       + describe (event));
            const auto lane = static_cast<std::size_t> (role - pitches.begin());
            auto& start = starts[event.bytes[1]];
            if (isOn (event))
            {
                const int packet = static_cast<int> ((event.sample - sourceOffset) / spacing);
                CHECK (packet >= 0 && packet < packetCount);
                CHECK_MSG (event.bytes[1] >= 48, "lower roles must not enter the muddy register below C3");
                velocitySum[lane] += event.bytes[2];
                ++counts[lane];
                CHECK_MSG (event.bytes[2] >= 1 && event.bytes[2] <= sourceVelocity * 48 / 100,
                           "every lane starts and remains markedly below its source velocity");
                lanes[packet][lane].push_back (event);
                start = event.sample;
            }
            else
            {
                CHECK (start >= 0);
                gates[lane].push_back (event.sample - start);
                start = -1;
            }
        }
        for (int packet = 0; packet < packetCount; ++packet)
        {
            std::array<Sample, 3> firstTimes {};
            for (int lane = 0; lane < 7; ++lane)
            {
                const auto& events = lanes[packet][lane];
                CHECK_MSG (! events.empty(), "each isolated source needs every enabled role; lane="
                           + std::to_string (lane) + " packet=" + std::to_string (packet)
                           + " seed=" + std::to_string (seed));
                if (lane < 3) firstTimes[lane] = events.front().sample;
                if (events.size() < 2) continue; // blocked candidates need not become audible events
                int earlyPeak = 0, latePeak = 0;
                std::vector<Sample> gaps;
                for (std::size_t i = 0; i < events.size(); ++i)
                {
                    auto& peak = i < (events.size() + 1) / 2 ? earlyPeak : latePeak;
                    peak = std::max (peak, static_cast<int> (events[i].bytes[2]));
                    const double elapsedBeats = static_cast<double> (events[i].sample - events.front().sample) / beat;
                    CHECK_MSG (events[i].bytes[2] <= std::max (1.0, sourceVelocity * 0.48
                                   * std::exp (-0.14 * elapsedBeats) + 0.5),
                               "fluctuating lane dynamics must remain below a decaying exponential upper bound");
                    if (i != 0)
                    {
                        rises[lane] += events[i].bytes[2] > events[i - 1].bytes[2];
                        falls[lane] += events[i].bytes[2] < events[i - 1].bytes[2];
                        gaps.push_back (events[i].sample - events[i - 1].sample);
                    }
                }
                earlyPeakSum[lane] += earlyPeak;
                latePeakSum[lane] += latePeak;
                // Compare a sufficient population of inter-onset gaps, not one
                // short first packet: two audible notes have only one gap and
                // cannot themselves demonstrate nonuniform spacing.
                allGaps[lane].insert (allGaps[lane].end(), gaps.begin(), gaps.end());
                seedGaps[lane].insert (seedGaps[lane].end(), gaps.begin(), gaps.end());
                seedGaps[lane].push_back (-1); // retain packet boundaries in each seed's gap signature
            }
            std::sort (firstTimes.begin(), firstTimes.end());
            CHECK_MSG (firstTimes[1] - firstTimes[0] >= beat / 8
                    && firstTimes[2] - firstTimes[1] >= beat / 8,
                       "first lane replies must be staggered by >=.125 beat, not a chord column");
        }
        for (int lane = 0; lane < 7; ++lane)
            seedGapShapes[lane].push_back (std::move (seedGaps[lane]));
    }
    for (std::uint32_t seed : { 1u, 19u, 73u, 101u })
    {
        auto control = settings (OutputMode::add, 0.30f);
        control.seed = seed;
        control.scale = Scale::major;
        Render render ({ note (sourceOffset, true, 62, sourceVelocity),
                         note (sourceOffset + 240, false, 62, 0) }, control);
        render.until (sourceOffset + replyTail, 2048);
        const auto generated = generatedInAdd (render.output, render.input);
        safety (generated);
        CHECK_MSG (onCount (generated) >= 3,
                   "default Amount keeps three roles present through their finite decay");
        std::array<Sample, 3> firstTimes {{ -1, -1, -1 }};
        for (const auto& event : generated)
            if (isOn (event))
            {
                const auto lane = dInCMajorLane (event.bytes[1]);
                CHECK (event.bytes[1] == 65 || event.bytes[1] == 69 || event.bytes[1] == 74);
                CHECK (event.bytes[2] <= sourceVelocity * 48 / 100);
                if (firstTimes[lane] < 0) firstTimes[lane] = event.sample;
            }
        CHECK_MSG (std::all_of (firstTimes.begin(), firstTimes.end(), [] (Sample at) { return at >= 0; }),
                   "every default isolated seed must emit third, fifth and octave, not randomly select one role");
        std::sort (firstTimes.begin(), firstTimes.end());
        CHECK (firstTimes[1] - firstTimes[0] >= beat / 8 && firstTimes[2] - firstTimes[1] >= beat / 8);
    }
    for (int lane = 0; lane < 7; ++lane)
        std::cout << "[ LANE ] pitch=" << pitches[lane] << " ons=" << counts[lane]
                  << " rises=" << rises[lane] << " falls=" << falls[lane]
                  << " earlyPeakSum=" << earlyPeakSum[lane] << " latePeakSum=" << latePeakSum[lane]
                  << " medianGateBeats=" << static_cast<double> (median (gates[lane])) / beat << '\n';
    for (int lane = 0; lane < 7; ++lane)
    {
        CHECK_MSG (rises[lane] > 0 && falls[lane] > 0,
                   "each lane needs observable local rises and falls across isolated packets; lane="
                   + std::to_string (lane) + " rises=" + std::to_string (rises[lane])
                   + " falls=" + std::to_string (falls[lane]));
        CHECK_MSG (latePeakSum[lane] < earlyPeakSum[lane],
                   "each lane's aggregate late peaks must be below its early peaks");
        auto& shapes = seedGapShapes[lane];
        auto& gaps = allGaps[lane];
        std::sort (gaps.begin(), gaps.end());
        gaps.erase (std::unique (gaps.begin(), gaps.end()), gaps.end());
        CHECK_MSG (gaps.size() >= 2, "each lane needs varying gaps across packets, not constant linear spacing");
        std::sort (shapes.begin(), shapes.end());
        shapes.erase (std::unique (shapes.begin(), shapes.end()), shapes.end());
        CHECK_MSG (shapes.size() >= 2, "different RNG seeds must alter each lane's gap shape");
        std::cout << "[ SHAPES ] pitch=" << pitches[lane] << " seedGapShapes=" << shapes.size()
                  << " distinctGaps=" << gaps.size() << '\n';
    }
    CHECK_MSG (median (gates[2]) >= median (gates[0]) && median (gates[2]) >= median (gates[1]),
               "octave lane median gate must be at least as long as third/fifth lanes");
    for (int lane = 0; lane < 3; ++lane)
        CHECK_MSG (static_cast<double> (velocitySum[lane + 3]) / counts[lane + 3]
                     < static_cast<double> (velocitySum[lane]) / counts[lane],
                   "each lower role must be lighter on average than its upper counterpart");
    for (auto scale : { Scale::unset, Scale::major })
        for (int velocity : { 1, 100 })
        {
            auto control = settings (OutputMode::particles, 0.30f);
            control.scale = scale;
            const int source = scale == Scale::unset ? 62 : 61; // off-scale C# in C major
            Render render ({ note (sourceOffset, true, source, velocity),
                             note (sourceOffset + 240, false, source, 0) }, control);
            render.until (sourceOffset + replyTail, 2048);
            safety (render.output);
            CHECK_MSG (onCount (render.output) > 0,
                       "unset/off-scale fallback keeps an audible finite octave line");
            for (const auto& event : render.output)
                if (isOn (event))
                {
                    CHECK (event.bytes[1] > source && event.bytes[1] % 12 == source % 12);
                    CHECK (event.bytes[2] >= 1 && event.bytes[2] <= std::max (1, velocity * 48 / 100));
                }
        }
    for (auto scale : { Scale::unset, Scale::major })
    {
        auto control = settings();
        control.scale = scale;
        constexpr int lowSource = 50; // D3: its lower octave is below the C3 floor
        Render render ({ note (sourceOffset, true, lowSource, sourceVelocity),
                         note (sourceOffset + 240, false, lowSource, 0) }, control);
        render.until (sourceOffset + replyTail, 2048);
        safety (render.output);
        CHECK (onCount (render.output) > 0);
        for (const auto& event : render.output)
            if (isOn (event))
            {
                CHECK_MSG (event.bytes[1] >= 48, "high Amount must omit inadmissible lower roles below C3");
                if (scale == Scale::unset)
                    CHECK (event.bytes[1] == lowSource || event.bytes[1] == lowSource + 12);
            }
    }
    {
        auto control = settings();
        control.scale = Scale::major;
        Render render ({ note (sourceOffset, true, 73, sourceVelocity),
                         note (sourceOffset + 240, false, 73, 0) }, control);
        render.until (sourceOffset + replyTail, 2048);
        safety (render.output);
        std::array<bool, 3> heard {};
        for (const auto& event : render.output)
            if (isOn (event))
            {
                CHECK_MSG (event.bytes[1] == 61 || event.bytes[1] == 73 || event.bytes[1] == 85,
                           "off-scale high Amount allows only unison and two octaves, not duplicate harmony fallbacks");
                heard[(event.bytes[1] - 61) / 12] = true;
            }
        CHECK (heard[0] && heard[1] && heard[2]);
    }
    // These are MIDI event/dynamic contracts, not a listening or musicality verdict.
}

void testMusicalGridAndSourceVelocity()
{
    // A small single-note fixture: off-grid source/host positions, ordinary Amount,
    // and one fixed seed. Velocity must change dynamics, not timing or pitch draws.
    constexpr int packetCount = 4;
    constexpr Sample spacing = isolatedSpacing;
    constexpr Sample sourceOffset = 117;
    for (double bpm : { 90.0, 120.0, 173.0 })
    {
        const double beatSamples = sampleRate * 60.0 / bpm;
        const double gridSamples = beatSamples / 8.0;
        Trace reference;
        for (int velocity : { 40, 80, 120 })
        {
            Trace input;
            for (int packet = 0; packet < packetCount; ++packet)
            {
                const Sample at = sourceOffset + packet * spacing;
                input.push_back (note (at, true, 62, velocity));
                input.push_back (note (at + 240, false, 62, 0));
            }
            auto control = settings (OutputMode::add, 0.30f);
            control.seed = 19u;
            control.root = 0;
            control.scale = Scale::major;
            Render render (input, control);
            render.setTempo (bpm);
            render.seek (bpm == 90.0 ? -12345 : 9876, bpm == 90.0 ? -7.137 : 9.271);
            render.until ((packetCount - 1) * spacing + sourceOffset + replyTail, 2048);
            const auto generated = generatedInAdd (render.output, render.input);
            safety (generated);
            CHECK_MSG (onCount (generated) >= packetCount * 2,
                       "fixture reply count=" + std::to_string (onCount (generated))
                       + " at bpm=" + std::to_string (bpm));
            // Interleaved roles can have short aggregate IOIs despite unhurried
            // individual lines. Check each observable pitch-class lane separately,
            // never impose a half-beat median on the merged three-lane stream.
            std::array<std::vector<Sample>, 3> intervals;
            std::array<Sample, 128> starts;
            starts.fill (-1);
            std::array<int, 3> previousPacket {{ -1, -1, -1 }};
            std::array<Sample, 3> previousOn {};
            for (const auto& event : generated)
            {
                const int packet = static_cast<int> ((event.sample - sourceOffset) / spacing);
                CHECK (packet >= 0 && packet < packetCount);
                const Sample offset = event.sample - (sourceOffset + packet * spacing);
                const double nearest = std::round (offset / gridSamples) * gridSamples;
                CHECK_MSG (std::abs (offset - nearest) <= 1.0 + 1.0e-6,
                           "On/Off must remain on source-relative .125-beat grid: " + describe (event));
                auto& start = starts[event.bytes[1]];
                if (isOn (event))
                {
                    CHECK (event.bytes[2] >= 1 && event.bytes[2] <= std::max (1, velocity * 48 / 100));
                    const int lane = dInCMajorLane (event.bytes[1]);
                    if (packet != previousPacket[lane])
                        CHECK_MSG (offset + 1.0 >= beatSamples * 0.25, "first reply is at least .25 beat later");
                    else
                        intervals[lane].push_back (event.sample - previousOn[lane]);
                    previousPacket[lane] = packet;
                    previousOn[lane] = event.sample;
                    start = event.sample;
                }
                else
                {
                    CHECK (start >= 0);
                    const double gate = event.sample - start;
                    CHECK_MSG (std::abs (gate - beatSamples * 0.5) <= 1.0
                            || std::abs (gate - beatSamples * 0.75) <= 1.0
                            || std::abs (gate - beatSamples) <= 1.0
                            || std::abs (gate - beatSamples * 1.5) <= 1.0,
                               "ordinary gates retain real .5/.75/1/1.5-beat note values");
                    start = -1;
                }
            }
            for (int lane = 0; lane < 3; ++lane)
                CHECK_MSG (median (intervals[lane]) + 1.0 >= beatSamples * 0.5,
                           "ordinary Amount per-lane median IOI must be at least half a beat");
            // Local rises and late peaks are checked independently by the role case.
            if (velocity == 40)
                reference = generated;
            else
            {
                CHECK (generated.size() == reference.size());
                for (std::size_t i = 0; i < generated.size(); ++i)
                {
                    CHECK (generated[i].sample == reference[i].sample);
                    CHECK (generated[i].bytes[0] == reference[i].bytes[0]);
                    CHECK (generated[i].bytes[1] == reference[i].bytes[1]);
                    // Each independently rounded MIDI velocity is within half a unit.
                    CHECK_MSG (std::abs (generated[i].bytes[2] / static_cast<double> (velocity)
                                      - reference[i].bytes[2] / 40.0) <= 0.5 / velocity + 0.5 / 40.0 + 1.0e-6,
                               "each reply scales from its own source velocity, not the previous reply");
                }
            }
        }
    }
}

void testSustainedDecayLifecycle()
{
    constexpr Sample beat = second / 2;
    const std::array<int, 3> upperPitches {{ 77, 81, 86 }};
    auto control = settings (OutputMode::add, 0.30f);
    control.scale = Scale::major;
    Render ordinary ({ note (0, true, 74), note (240, false, 74, 0) }, control);
    ordinary.until (replyTail, 2048);
    const auto generated = generatedInAdd (ordinary.output, ordinary.input);
    safety (generated);
    for (int pitch : upperPitches)
    {
        Trace lane;
        for (const auto& event : generated)
            if (isOn (event) && event.bytes[1] == pitch) lane.push_back (event);
        CHECK_MSG (lane.size() > 3, "default upper lane must outlive the old three-candidate budget; pitch="
                   + std::to_string (pitch));
        CHECK_MSG (lane.back().sample > 3 * second, "each default upper lane must continue beyond three seconds");
        CHECK (lane.back().sample < 32 * beat);
    }
    CHECK (ordinary.status().generatedNoteOns == onCount (generated));
    CHECK (ordinary.status().generatedKeysDown == 0 && ordinary.status().generatedHeld == 0);

    // A held original blocks unison without spending future replies or resetting
    // the PLANNED first-beat decay clock on release.
    control.amount = 1.0f;
    Render blocked ({ note (0, true, 74), note (3 * second, false, 74, 0) }, control);
    blocked.until (3 * second, 2048);
    const auto beforeRelease = generatedInAdd (blocked.output, blocked.consumedInput());
    CHECK (std::none_of (beforeRelease.begin(), beforeRelease.end(), [] (const Event& event) {
        return isOn (event) && event.bytes[1] == 74;
    }));
    CHECK (blocked.status().skippedCandidates > 0);
    blocked.until (replyTail, 2048);
    const auto afterRelease = generatedInAdd (blocked.output, blocked.input);
    safety (afterRelease);
    Trace unison;
    for (const auto& event : afterRelease)
        if (isOn (event) && event.bytes[1] == 74)
        {
            CHECK (event.sample >= 3 * second && event.sample < 32 * beat);
            const double elapsed = static_cast<double> (event.sample - beat) / beat;
            CHECK_MSG (event.bytes[2] <= 100.0 * 0.22 * std::exp (-0.14 * elapsed) + 0.5,
                       "blocked unison must decay from its planned first beat, not restart at release");
            if (! unison.empty())
                CHECK_MSG (event.sample - unison.back().sample >= 3 * beat / 4,
                           "unison resumes musical gaps, never burst-catches up missed attempts");
            unison.push_back (event);
        }
    CHECK_MSG (unison.size() >= 2, "blocked unison must retain at least two actual replies after release");
    CHECK (blocked.status().generatedNoteOns == onCount (afterRelease));

    // Hold every default target with Amount=0 before triggering a new cloud.
    // Missed candidates must not count as actual emitted Ons, including its first.
    control.amount = 0.0f;
    Trace heldTargets;
    for (int pitch : upperPitches) heldTargets.push_back (note (0, true, pitch));
    Render pressure (heldTargets, control);
    pressure.step (1);
    pressure.controls.amount = 0.30f;
    const Sample trigger = pressure.now;
    pressure.enqueue (note (trigger, true, 74));
    pressure.enqueue (note (trigger + 240, false, 74, 0));
    pressure.until (trigger + 3 * second, 2048);
    CHECK (pressure.status().generatedNoteOns == 0 && pressure.status().skippedCandidates > 0);
    CHECK (generatedInAdd (pressure.output, pressure.consumedInput()).empty());
    for (int pitch : upperPitches) pressure.enqueue (note (pressure.now, false, pitch, 0));
    pressure.until (trigger + replyTail, 2048);
    const auto recovered = generatedInAdd (pressure.output, pressure.input);
    safety (recovered);
    CHECK (onCount (recovered) >= 2);
    CHECK (pressure.status().generatedNoteOns == onCount (recovered));
    for (const auto& event : recovered)
        if (isOn (event))
        {
            CHECK (event.sample >= trigger + 3 * second && event.sample < trigger + 32 * beat);
            const double planned = event.bytes[1] == 77 ? 0.75 : event.bytes[1] == 81 ? 1.5 : 0.5;
            const double elapsed = static_cast<double> (event.sample - trigger) / beat - planned;
            CHECK_MSG (event.bytes[2] <= 100.0 * 0.45 * std::exp (-0.14 * elapsed) + 0.5,
                       "first actual On after collisions retains the planned exponential upper bound");
        }

    // Slow tempo exercises the wall-clock cap with unavailable low-register roles.
    control.amount = 1.0f;
    Render slow ({ note (0, true, 0), note (240, false, 0, 0) }, control);
    slow.setTempo (30.0);
    slow.until (replyTail, 2048);
    const auto slowGenerated = generatedInAdd (slow.output, slow.input);
    CHECK (onCount (slowGenerated) > 0);
    CHECK (onCount (slowGenerated, 24 * second) == 0);
    safety (slowGenerated);
    slow.controls.outputMode = OutputMode::particles;
    slow.step (32);
    CHECK (slow.status().activeMode == OutputMode::particles && ! slow.status().modeChangePending);

    // Cancellation discards long-lived lanes, not merely their next On.
    control.amount = 0.30f;
    Render interrupted ({ note (0, true, 74), note (240, false, 74, 0) }, control);
    interrupted.waitForGeneratedKey();
    const Sample cutoff = interrupted.now;
    interrupted.controls.amount = 0.0f;
    interrupted.step (32);
    CHECK (interrupted.status().generatedKeysDown == 0);
    interrupted.until (replyTail, 2048);
    interrupted.controls.amount = 0.30f;
    interrupted.forSeconds (2.0, 2048);
    const auto canceled = generatedInAdd (interrupted.output, interrupted.input);
    CHECK (onCount (canceled, cutoff) == 0);
    safety (canceled);
}

void testManualWorkbench()
{
    // One aggregate fixture; the original smoke cases still use manualLanes=false.
    CHECK (! Controls {}.manualLanes);
    auto control = settings (OutputMode::add, 0.30f);
    control.manualLanes = true;
    for (auto& lane : control.lanes) lane.enabled = false;
    control.lanes[3] = { true, 4, 6, 0.80f, 0.40f, 2, 0.0f, 2 };
    CHECK (noteBeats[4] == 0.5 && noteBeats[6] == 1.0 && noteBeats[2] == 0.25);
    CHECK (noteBeats[1] == 0.1875);
    const Trace source { note (0, true, 60, 100), note (240, false, 60, 0) };
    const auto finish = [] (Render& render) {
        render.until (52 * second, 2048); // Include manual hard lifetime and final gate.
        auto generated = generatedInAdd (render.output, render.input);
        safety (generated);
        CHECK (render.status().generatedNoteOns == onCount (generated));
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
        return generated;
    };
    const auto ons = [] (const Trace& trace) {
        Trace result;
        for (const auto& event : trace) if (isOn (event)) result.push_back (event);
        return result;
    };
    const auto checkPairs = [] (const Trace& trace, Sample origin, double bpm,
                               double first, double repeat, double gate, int pitch) {
        CHECK (! trace.empty() && trace.size() % 2 == 0);
        const double beatSamples = sampleRate * 60.0 / bpm;
        for (std::size_t i = 0; i < trace.size(); i += 2)
        {
            const auto& on = trace[i];
            const auto& off = trace[i + 1];
            CHECK (isOn (on) && isOff (off));
            CHECK (on.bytes[1] == pitch && off.bytes[1] == pitch);
            CHECK (on.bytes[0] == 0x90 && off.bytes[0] == 0x80);
            const double planned = origin + (first + (i / 2) * repeat) * beatSamples;
            CHECK_MSG (std::abs (on.sample - planned) <= 1.0,
                       "manual first/repeat must be exact musical time: " + describe (on));
            CHECK_MSG (std::abs (off.sample - (planned + gate * beatSamples)) <= 1.0,
                       "manual gate must retain its exact beat endpoint: " + describe (off));
            CHECK (std::abs ((off.sample - on.sample) - gate * beatSamples) <= 1.0);
            for (const auto* event : { &on, &off })
            {
                const double cells = (event->sample - origin) / (beatSamples / 16.0);
                CHECK_MSG (std::abs (cells - std::round (cells)) * beatSamples / 16.0 <= 1.0,
                           "manual On/Off must stay on the source-relative quarter/16 grid");
            }
        }
    };

    Render baseline (source, control);
    const auto reference = finish (baseline);
    const auto referenceOns = ons (reference);
    checkPairs (reference, 0, 120.0, 0.5, 1.0, 0.25, 72);
    CHECK (referenceOns.size() > 3 && referenceOns.back().sample > 3 * second);
    CHECK (referenceOns[0].sample == 12000 && referenceOns[0].bytes[2] == 40);
    CHECK (referenceOns[1].sample == 36000 && referenceOns[1].bytes[2] == 32);
    CHECK (referenceOns[2].sample == 60000 && referenceOns[2].bytes[2] == 26);
    std::size_t expectedCount = 0;
    for (double upper = 0.40; upper > 0.035; upper *= 0.80) ++expectedCount;
    CHECK_MSG (referenceOns.size() == expectedCount, "manual lane ends on its upper envelope, not a note quota");
    for (std::size_t i = 0; i < referenceOns.size(); ++i)
    {
        CHECK (referenceOns[i].sample == 12000 + static_cast<Sample> (i) * 24000);
        CHECK (reference[i * 2 + 1].sample == referenceOns[i].sample + 6000);
        CHECK (referenceOns[i].bytes[2] == std::lround (100.0 * 0.40 * std::pow (0.80, i)));
    }

    std::array<Trace, 3> feedbackOns;
    const std::array<float, 3> feedback {{ 0.0f, 0.4f, 0.9f }};
    for (std::size_t i = 0; i < feedback.size(); ++i)
    {
        auto changed = control;
        changed.lanes[3].feedback = feedback[i];
        Render render (source, changed);
        const auto generated = finish (render);
        checkPairs (generated, 0, 120.0, 0.5, 1.0, 0.25, 72);
        feedbackOns[i] = ons (generated);
    }
    CHECK (feedbackOns[0].size() == 1 && feedbackOns[0][0].sample > source.back().sample);
    CHECK (feedbackOns[0][0].bytes[2] == 40);
    CHECK (feedbackOns[2].size() > feedbackOns[1].size());
    CHECK (feedbackOns[2].back().sample > feedbackOns[1].back().sample);

    auto disabled = control;
    disabled.lanes[3].enabled = false;
    Render silent (source, disabled);
    CHECK (finish (silent).empty());
    sameTrace (source, silent.output, "all manual lanes disabled: source only");

    // Non-integral tempo, dotted first, and off-grid source/host positions must
    // not gain a millisecond offset or drift with independent MIDI rounding.
    Trace quiet;
    for (int velocity : { 40, 80, 120 })
    {
        auto dotted = control;
        dotted.lanes[3].first = 1;
        Render render ({ note (117, true, 60, velocity), note (357, false, 60, 0) }, dotted);
        render.setTempo (173.0);
        render.seek (9876, 9.271);
        const auto generated = finish (render);
        checkPairs (generated, 117, 173.0, 0.1875, 1.0, 0.25, 72);
        CHECK (onCount (generated) == expectedCount);
        for (std::size_t i = 0; i < generated.size(); ++i)
        {
            if (isOn (generated[i]))
                CHECK (generated[i].bytes[2] == std::lround (velocity * 0.40 * std::pow (0.80, i / 2)));
            if (velocity != 40)
            {
                CHECK (generated[i].sample == quiet[i].sample);
                CHECK (generated[i].bytes[0] == quiet[i].bytes[0] && generated[i].bytes[1] == quiet[i].bytes[1]);
                CHECK (std::abs (generated[i].bytes[2] / static_cast<double> (velocity)
                              - quiet[i].bytes[2] / 40.0) <= 0.5 / velocity + 0.5 / 40.0 + 1.0e-6);
            }
        }
        if (velocity == 40) quiet = generated;
    }

    // A blocked planned first still spends decay time, never restarting at 40.
    auto unison = control;
    unison.lanes[0] = unison.lanes[3];
    unison.lanes[3].enabled = false;
    Render blocked ({ note (0, true, 60), note (2 * second, false, 60, 0) }, unison);
    const auto recovered = finish (blocked);
    CHECK (blocked.status().skippedCandidates > 0 && onCount (recovered) >= 2);
    for (const auto& event : recovered)
        if (isOn (event))
        {
            CHECK (event.bytes[1] == 60 && event.sample >= 2 * second);
            CHECK ((event.sample - 12000) % 24000 == 0);
            const double elapsed = (event.sample - 12000) / 24000.0;
            CHECK (event.bytes[2] == std::lround (40.0 * std::pow (0.80, elapsed)));
            CHECK (event.bytes[2] < 40);
        }

    // Disable kills pending Ons, not the sounding voice's scheduled Off. Merely
    // re-enabling cannot revive the old cloud; a new source can create a new one.
    Render toggled (source, control);
    toggled.until (12001);
    CHECK (toggled.status().generatedKeysDown == 1);
    toggled.controls.lanes[3].enabled = false;
    toggled.until (15000);
    CHECK (toggled.status().generatedKeysDown == 1);
    toggled.controls.lanes[3].enabled = true;
    const auto canceled = finish (toggled);
    sameTrace ({ note (12000, true, 72, 40), note (18000, false, 72, 0) }, canceled,
               "lane disable preserves owned gate Off and permanently cancels future");
    const Sample fresh = toggled.now;
    toggled.enqueue (note (fresh, true, 60));
    toggled.enqueue (note (fresh + 240, false, 60, 0));
    toggled.until (fresh + replyTail, 2048);
    Trace freshGenerated;
    for (const auto& event : generatedInAdd (toggled.output, toggled.input))
        if (event.sample >= fresh) freshGenerated.push_back (event);
    Trace shifted = reference;
    for (auto& event : shifted) event.sample += fresh;
    sameTrace (shifted, freshGenerated, "re-enabled lane starts only with a fresh source");

    // Timing alone is a next-source snapshot. Test first by itself as well as
    // all three fields, before the old source has emitted its first reply.
    for (bool firstOnly : { true, false })
    {
        Render timing (source, control);
        timing.until (6000);
        timing.controls.lanes[3].first = 6;
        if (! firstOnly)
        {
            timing.controls.lanes[3].repeat = 8;
            timing.controls.lanes[3].gate = 4;
        }
        sameTrace (reference, finish (timing), "timing automation must not restart or retime old source");
        const Sample at = timing.now;
        timing.enqueue (note (at, true, 60));
        timing.enqueue (note (at + 240, false, 60, 0));
        timing.until (at + replyTail, 2048);
        Trace next;
        for (const auto& event : generatedInAdd (timing.output, timing.input))
            if (event.sample >= at) next.push_back (event);
        safety (next);
        checkPairs (next, at, 120.0, 1.0, firstOnly ? 1.0 : 2.0, firstOnly ? 0.25 : 0.5, 72);
    }

    // Level and feedback are live on future events without another source On.
    for (bool changeFeedback : { false, true })
    {
        Render live (source, control);
        live.until (24000);
        if (changeFeedback) live.controls.lanes[3].feedback = 0.4f;
        else live.controls.lanes[3].level = 0.2f;
        const auto generated = finish (live);
        const auto future = ons (generated);
        CHECK (future.size() >= 2 && future[0].bytes[2] == 40);
        CHECK (future[1].sample == 36000 && future[1].bytes[2] == 16);
        CHECK (future.size() < referenceOns.size());
        const double level = changeFeedback ? 0.4 : 0.2;
        const double decay = changeFeedback ? 0.4 : 0.8;
        for (std::size_t i = 1; i < future.size(); ++i)
            CHECK (future[i].bytes[2] == std::lround (100.0 * level * std::pow (decay, i)));
        CHECK (level * std::pow (decay, future.size() - 1) > 0.035);
        CHECK (level * std::pow (decay, future.size()) <= 0.035);
        checkPairs (generated, 0, 120.0, 0.5, 1.0, 0.25, 72);
    }

    // Motion/cycle change subsequent dynamics, not the timing snapshot or the
    // independently decaying upper bound. All comparisons reuse one source/seed.
    std::array<Trace, 3> waves;
    for (std::size_t variant = 0; variant < waves.size(); ++variant)
    {
        auto moving = control;
        moving.lanes[3].motion = 0.8f;
        Render live (source, moving);
        live.until (24000);
        if (variant == 1) live.controls.lanes[3].motion = 0.0f;
        if (variant == 2) live.controls.lanes[3].cycle = 3;
        waves[variant] = finish (live);
        CHECK (waves[variant].size() == reference.size());
        for (std::size_t i = 0; i < reference.size(); ++i)
        {
            CHECK (waves[variant][i].sample == reference[i].sample);
            CHECK (waves[variant][i].bytes[1] == reference[i].bytes[1]);
            CHECK (waves[variant][i].bytes[2] <= reference[i].bytes[2]);
        }
        CHECK (sameEvent (waves[variant][0], reference[0]));
    }
    sameTrace (reference, waves[1], "live motion=0 removes modulation without restarting decay");
    for (std::size_t variant : { std::size_t { 1 }, std::size_t { 2 } })
        CHECK_MSG (! std::equal (waves[0].begin(), waves[0].end(), waves[variant].begin(), sameEvent),
                   "live motion/cycle must affect events from the existing source");

    for (float bad : { std::numeric_limits<float>::quiet_NaN(),
                       std::numeric_limits<float>::infinity(), -100.0f, 100.0f })
    {
        auto invalid = control;
        auto& lane = invalid.lanes[3];
        lane.first = lane.gate = std::numeric_limits<int>::min();
        lane.repeat = lane.cycle = std::numeric_limits<int>::max();
        lane.level = lane.feedback = lane.motion = bad;
        Render render (source, invalid);
        const auto generated = finish (render);
        CHECK (onCount (generated) <= 48 * 48);
        if (bad == 100.0f) CHECK (! generated.empty()); // Clamped high values still exercise generation.
        for (const auto& event : generated)
        {
            CHECK (event.bytes[1] == 72 && event.sample >= 0 && event.sample < 52 * second);
            CHECK (event.bytes[2] <= 100); // Invalid values cannot exceed the source velocity.
        }
        // Invalid controls must not poison the clock or a later valid source.
        render.controls = control;
        const Sample at = render.now;
        render.enqueue (note (at, true, 60));
        render.enqueue (note (at + 240, false, 60, 0));
        render.until (at + replyTail, 2048);
        Trace next;
        for (const auto& event : generatedInAdd (render.output, render.input))
            if (event.sample >= at) next.push_back (event);
        Trace expected = reference;
        for (auto& event : expected) event.sample += at;
        sameTrace (expected, next, "invalid fields leave a finite, recoverable engine");
    }

    // Reuse the fixed-size allocation probe, never the allocating trace sink.
    ParticleEngine engine;
    engine.prepare (sampleRate);
    FixedInput input;
    FixedOutput output;
    output.frames = 24000;
    Transport transport;
    transport.hasTempo = transport.hasPpq = transport.hasSamplePosition = transport.isPlaying = true;
    for (int block = 0; block < 52; ++block)
    {
        input.count = input.cursor = output.count = 0;
        output.previous = -1;
        if (block == 0)
        {
            input.add (0, { 0x90, 60, 100 });
            input.add (240, { 0x80, 60, 0 });
        }
        transport.samplePosition = static_cast<Sample> (block) * output.frames;
        transport.ppqPosition = block;
        {
            AllocationAudit::Scope probe;
            engine.process (output.frames, transport, control, input, output);
        }
        CHECK (AllocationAudit::allocations == 0 && AllocationAudit::deallocations == 0);
        CHECK (output.valid && input.cursor == input.count);
    }
    CHECK (engine.getStatus().generatedNoteOns == expectedCount);
    CHECK (engine.getStatus().generatedKeysDown == 0 && engine.getStatus().generatedHeld == 0);
}

void testInputPriorityWindow()
{
    Trace input;
    for (int strike = 0; strike < 160; ++strike)
    {
        const Sample at = strike * 3203;
        input.push_back (note (at, true, 48 + strike % 24));
        input.push_back (note (at + 240, false, 48 + strike % 24, 0));
    }
    Render render (input, settings());
    render.until (18 * second, 512);
    CHECK (onCount (render.output) > 0);
    for (const auto& event : render.output)
        if (isOn (event))
            for (const auto& original : render.input)
                if (isOn (original) && original.sample <= event.sample)
                    CHECK_MSG (event.sample - original.sample >= 1680, "reply enters global 35ms source-On window");
    safety (render.output);
}

bool inScale (int pitch, int root, Scale scale)
{
    const int pc = ((pitch - root) % 12 + 12) % 12;
    const std::array<int, 7> major {{ 0, 2, 4, 5, 7, 9, 11 }};
    const std::array<int, 7> minor {{ 0, 2, 3, 5, 7, 8, 10 }};
    const auto& degrees = scale == Scale::major ? major : minor;
    return std::find (degrees.begin(), degrees.end(), pc) != degrees.end();
}

void testScaleExamplesAndFallback()
{
    struct Example { int source, root, third, fifth; Scale scale; };
    for (const auto& example : std::array<Example, 3> {{
             { 62, 0, 5, 9, Scale::major }, // D4 -> F and A, never F#
             { 71, 9, 2, 5, Scale::minor }, // B4 -> D and F, never D#
             { 61, 0, -1, -1, Scale::major } }})
    {
        bool third = false, fifth = false;
        for (std::uint32_t seed : { 19u, 73u })
        {
            auto control = settings (OutputMode::particles, 0.30f); // default keeps the three upper roles
            control.seed = seed;
            control.root = example.root;
            control.scale = example.scale;
            Render render (packets (example.source, 48), control);
            render.until (47 * isolatedSpacing + replyTail, 2048);
            CHECK (onCount (render.output) > 0);
            for (const auto& event : render.output)
                if (isOn (event))
                {
                    const int pc = event.bytes[1] % 12;
                    if (example.third < 0)
                        CHECK_MSG (pc == example.source % 12, "off-scale C# only gets original/octave fallback");
                    else
                    {
                        CHECK (inScale (event.bytes[1], example.root, example.scale));
                        CHECK (pc == example.source % 12 || pc == example.third || pc == example.fifth);
                        third = third || pc == example.third;
                        fifth = fifth || pc == example.fifth;
                    }
                }
            safety (render.output);
        }
        if (example.third >= 0)
            CHECK_MSG (third && fifth, "repeated input must explore diatonic third and fifth");
    }
}

void testAllRootsAndScales()
{
    for (int root = 0; root < 12; ++root)
        for (auto scale : { Scale::major, Scale::minor })
        {
            auto control = settings();
            control.root = root;
            control.scale = scale;
            Trace input;
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                const Sample at = pitch * 4 * second;
                input.push_back (note (at, true, pitch));
                input.push_back (note (at + 240, false, pitch, 0));
            }
            Render render (input, control);
            render.until ((128 * 4 + 2) * second, 1024); // drain longer final gates
            std::array<int, 128> counts {};
            for (const auto& event : render.output)
                if (isOn (event))
                {
                    const int source = static_cast<int> (event.sample / (4 * second));
                    CHECK (source >= 0 && source < 128);
                    ++counts[static_cast<std::size_t> (source)];
                    CHECK (event.bytes[1] < 128 && event.bytes[2] > 0 && event.bytes[2] <= 80);
                    if (inScale (source, root, scale))
                        CHECK_MSG (inScale (event.bytes[1], root, scale), "signed degree/octave math left current scale");
                    else
                        CHECK (event.bytes[1] % 12 == source % 12);
                }
            CHECK (std::all_of (counts.begin(), counts.end(), [] (int n) { return n > 0; }));
            safety (render.output);
        }
}

void testScaleAutomationCancelsFutureOnly()
{
    for (bool changeRoot : { false, true })
    {
        auto control = settings();
        control.scale = Scale::major;
        Render render ({ note (0, true, 62), note (240, false, 62, 0) }, control);
        render.waitForGeneratedKey();
        const Sample change = render.now;
        if (changeRoot) render.controls.root = 9;
        else render.controls.scale = Scale::minor;
        render.forSeconds (5.0, 128);
        CHECK_MSG (onCount (render.output, change) == 0, "root/scale changes cancel future old-seed candidates");
        balancedGenerated (render.output);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
        const Sample fresh = render.now;
        const int source = changeRoot ? 69 : 60;
        render.enqueue (note (fresh, true, source));
        render.enqueue (note (fresh + 240, false, source, 0));
        render.forSeconds (5.0, 128);
        CHECK (onCount (render.output, fresh) > 0);
        for (const auto& event : render.output)
            if (isOn (event) && event.sample >= fresh)
                CHECK (inScale (event.bytes[1], render.controls.root, render.controls.scale));
        safety (render.output);
    }
}

void testPolySourceOwnership()
{
    Trace input;
    for (int strike = 0; strike < 32; ++strike)
        for (int pitch : { 60, 64, 67 })
        {
            const Sample at = strike * 5 * second;
            const int channel = pitch == 60 ? 1 : pitch == 64 ? 2 : 16;
            input.push_back (note (at, true, pitch, 100, channel));
            input.push_back (note (at + 240, false, pitch, 0, channel));
        }
    Render render (input, settings());
    render.until (160 * second);
    CHECK (onCount (render.output) > 0);
    validOctaves (render.output, render.input);
    safety (render.output);
    std::array<bool, 16> heard {};
    for (const auto& event : render.output)
        if (isOn (event)) heard[event.bytes[0] & 15] = true;
    CHECK (heard[0] && heard[1] && heard[15]);

    // Same-channel packets really form chords. With all seven roles enabled,
    // the union of C/E/G and their diatonic +/-third/fifth covers all C-major PCs;
    // exact role ownership is checked by the isolated-source lane fixture.
    for (auto& event : input)
        event.bytes[0] = static_cast<std::uint8_t> (event.bytes[0] & 0xf0);
    auto control = settings();
    control.scale = Scale::major;
    Render chord (input, control);
    chord.until (160 * second);
    CHECK (onCount (chord.output) > 0);
    for (const auto& event : chord.output)
        if (isOn (event))
        {
            const int pc = event.bytes[1] % 12;
            CHECK_MSG (pc == 0 || pc == 2 || pc == 4 || pc == 5 || pc == 7 || pc == 9 || pc == 11,
                       "C/E/G seven-role union includes lower thirds/fifths F/A, but no chromatic tones");
        }
    safety (chord.output);
}

struct Test { const char* name; std::function<void()> run; };
}

int main (int argc, char** argv)
{
    const std::vector<Test> tests {
        { "Add Amount=0 exact bytes", testAddZero },
        { "Particles Amount=0 silence", testParticlesZero },
        { "dense pool pressure preserves Off ownership", testDenseLifecycle },
        { "amount interruption", testAmountInterrupt },
        { "bypass cancellation and recovery", testBypass },
        { "stop and fresh live input", [] { testStopLive (false); testStopLive (true); } },
        { "seek and loop cancellation", [] { testDiscontinuity (false); testDiscontinuity (true); } },
        { "velocity zero and repeated physical keys", testVelocityZero },
        { "collision cleanup before original; no stale Off", testCollisionNoStaleOff },
        { "CC64 shared sustain and voice budget", testSustainBudget },
        { "CC66 snapshot", testSostenutoSnapshot },
        { "pedal intersection", testPedalIntersection },
        { "CC123 All Notes Off", testAllNotesOff },
        { "CC120 All Sound Off", testAllSoundOff },
        { "CC121 controller reset preserves physical keys", testResetControllers },
        { "Particles channel messages emit owned Offs", testParticlesChannelMessages },
        { "mode waits for idle keys and pedals", testModePending },
        { "reset serial/seed cancellation", testHotStateLoad },
        { "abnormal and dynamic BPM finite", testAbnormalTempo },
        { "sample rates and missing transport fallback", testSampleRatesAndMissingClock },
        { "generated observation exact lanes/timestamps; unchanged MIDI", testGeneratedObservation },
        { "generated observation excludes false events", testObservationNoFalseEvents },
        { "process performs no C++ heap operations", testProcessNoHeap },
        { "seed determinism; five exact blocks; zero blocks", testDeterminismPartitionsAndZero },
        { "idle has no autonomous output", testIdle },
        { "all MIDI key boundaries; unset octave-only", testAllPitchBoundaries },
        { "one-sample input retains finite after-release reply", testShortNoteFiniteReply },
        { "six-note chord does not multiply density", testChordDensity },
        { "new melody packets preserve older finite replies", testMelodyPreservesOlderReplies },
        { "velocity ceiling; local rise; lower late peaks", testVelocityEnvelope },
        { "timing variety; independent packets; burst/rest", testTimingVarietyAndRests },
        { "musical source-relative grid and source velocity", testMusicalGridAndSourceVelocity },
        { "fixed lanes; sparse default; seven-role dynamics", testFixedLanesAndDynamics },
        { "sustained decay; blocked first On; finite safety", testSustainedDecayLifecycle },
        { "manual workbench timing; dynamics; lifecycle; safety", testManualWorkbench },
        { "global 35ms input priority", testInputPriorityWindow },
        { "diatonic thirds/fifths and off-scale fallback", testScaleExamplesAndFallback },
        { "all roots/scales across all MIDI pitches", testAllRootsAndScales },
        { "root/scale automation cancels old future seeds", testScaleAutomationCancelsFutureOnly },
        { "polyphonic replies retain input pitch/channel ownership", testPolySourceOwnership }
    };
    int failed = 0, selected = 0;
    const std::string filter = argc > 1 ? argv[1] : "";
    std::cout << "Single-purpose finite-reply contract; old multi-mode product tests retired.\n";
    for (const auto& test : tests)
    {
        if (filter == "--smoke")
        {
            const std::array<const char*, 12> essential {{
                "Add Amount=0 exact bytes", "dense pool pressure preserves Off ownership",
                "stop and fresh live input", "CC64 shared sustain and voice budget",
                "one-sample input retains finite after-release reply",
                "velocity ceiling; local rise; lower late peaks",
                "timing variety; independent packets; burst/rest",
                "diatonic thirds/fifths and off-scale fallback",
                "musical source-relative grid and source velocity",
                "fixed lanes; sparse default; seven-role dynamics",
                "sustained decay; blocked first On; finite safety",
                "manual workbench timing; dynamics; lifecycle; safety"
            }};
            if (std::none_of (essential.begin(), essential.end(), [&] (const char* name) {
                    return std::string (test.name) == name; }))
                continue;
        }
        else if (! filter.empty() && std::string (test.name).find (filter) == std::string::npos)
            continue;
        ++selected;
        try
        {
            test.run();
            std::cout << "[ PASS ] " << test.name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "[ FAIL ] " << test.name << ": " << error.what() << '\n';
        }
    }
    std::cout << selected - failed << '/' << selected << " passed, " << failed << " failed\n";
    if (selected == 0)
        std::cerr << "No test matched filter: " << filter << '\n';
    return failed == 0 && selected > 0 ? 0 : 1;
}
