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
constexpr int voiceBudget = 24;

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
                   "keys plus pedal-held generated occupancy exceeded the public 24-voice budget");
    }

    double rate_, bpm_ = 120.0;
    Sample anchorWall_ = 0, anchorSample_ = 0;
    double anchorPpq_ = 0.0;
    bool playing_ = true;
    std::size_t cursor_ = 0;
};

Controls settings (OutputMode mode = OutputMode::particles, Character character = Character::bloom,
                   float amount = 1.0f)
{
    Controls result;
    result.outputMode = mode;
    result.character = character;
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
        CHECK_MSG (right - left + 1 <= 24, "generated Note On rate exceeded 24 in a half-open one-second window");
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

void testAddZero()
{
    Render render (mixedMidi(), settings (OutputMode::add, Character::bloom, 0.0f));
    render.until (2 * second, 960);
    sameTrace (render.input, render.output, "Add Amount=0 byte passthrough");
    CHECK (render.status().generatedNoteOns == 0);
    CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
}

void testParticlesZero()
{
    Render render (mixedMidi(), settings (OutputMode::particles, Character::bloom, 0.0f));
    render.until (20 * second);
    CHECK (render.output.empty());
    CHECK (render.status().activeMode == OutputMode::particles);
    CHECK (render.status().generatedNoteOns == 0);
}

void testCharacter (Character character)
{
    for (float amount : { 0.30f, 1.0f })
    {
        Render render (musicalInput(), settings (OutputMode::particles, character, amount));
        render.until (30 * second);
        CHECK (onCount (render.output) > 0);
        validOctaves (render.output, render.input);
        balancedGenerated (render.output);
        boundedRate (render.output);
        CHECK (onCount (render.output, 20 * second) == 0);
        CHECK (render.status().generatedKeysDown == 0 && render.status().generatedHeld == 0);
    }
}

void testRegisterPolicy()
{
    Trace input;
    for (int strike = 0; strike < 80; ++strike)
    {
        const Sample start = strike * second / 2;
        input.push_back (note (start, true, 48, 100));
        input.push_back (note (start + 1000, false, 48, 0));
    }
    for (auto character : { Character::echo, Character::bloom, Character::pulse })
        for (float amount : { 0.3f, 1.0f })
        {
            Render render (input, settings (OutputMode::particles, character, amount));
            render.until (50 * second);
            bool secondOctave = false;
            for (const auto& event : render.output)
                if (isOn (event))
                {
                    const auto pitch = event.bytes[1];
                    CHECK (pitch == 48 || pitch == 60 || pitch == 72);
                    secondOctave = secondOctave || pitch == 72;
                    if (character == Character::echo || amount < 0.6f)
                        CHECK (pitch != 72);
                }
            if (character != Character::echo && amount == 1.0f)
                CHECK_MSG (secondOctave, "High Amount must permit the designed second octave");
            balancedGenerated (render.output);
        }
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

Trace renderForPartition (int block, OutputMode mode, Character character, bool zeroBlocks = false,
                          std::uint32_t seed = 0x6f7a6f31u)
{
    auto input = musicalInput();
    input.push_back (cc (101, 64, 127, 1));
    input.push_back (cc (2177, 66, 127, 2));
    input.push_back (cc (6 * second, 64, 0, 1));
    input.push_back (cc (6 * second + 59, 66, 0, 2));
    input.push_back ({ 1024, { 0xe3, 0x36, 0x45 } });
    input.push_back ({ 2048, { 0xf0, 0x7d, 0x05, 0xf7 } });
    auto control = settings (mode, character, 0.85f);
    control.seed = seed;
    Render render (input, control);
    render.insertZeroBlocks = zeroBlocks;
    // Identical absolute automation boundaries for every host block partition.
    render.until (2 * second + 117, block);
    render.controls.amount = 0.60f;
    render.until (4 * second + 29, block);
    render.controls.amount = 1.0f;
    render.until (25 * second, block);
    const auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
    CHECK (onCount (generated) > 0);
    balancedGenerated (generated);
    return render.output;
}

void testBlockPartitions (OutputMode mode, Character character)
{
    const auto reference = renderForPartition (32, mode, character);
    for (int block : { 64, 128, 512, 1024 })
        sameTrace (reference, renderForPartition (block, mode, character),
                   "full MIDI trace, block 32 versus " + std::to_string (block));
}

void testSeedReproducibility()
{
    for (std::uint32_t seed : { 0u, 0x6f7a6f31u, 0xffffffffu })
    {
        const auto first = renderForPartition (128, OutputMode::particles, Character::bloom, false, seed);
        const auto secondRun = renderForPartition (128, OutputMode::particles, Character::bloom, false, seed);
        sameTrace (first, secondRun, "repeat render with seed " + std::to_string (seed));
    }
}

void testChordRate()
{
    for (auto character : { Character::echo, Character::bloom, Character::pulse })
    {
        std::array<std::size_t, 2> totals {};
        for (int variant = 0; variant < 2; ++variant)
        {
            for (std::uint32_t seed : { 1u, 19u, 0x6f7a6f31u })
            {
                Trace input;
                for (int strike = 0; strike < 24; ++strike)
                    for (int tone = 0; tone < (variant == 0 ? 1 : 12); ++tone)
                    {
                        const Sample at = strike * second / 2;
                        input.push_back (note (at, true, 48 + tone, 105));
                        input.push_back (note (at + second / 5, false, 48 + tone, 0));
                    }
                auto control = settings (OutputMode::particles, character);
                control.seed = seed;
                Render render (input, control);
                render.until (30 * second);
                const auto count = onCount (render.output);
                CHECK (count > 0);
                totals[variant] += count;
                balancedGenerated (render.output);
                boundedRate (render.output);
            }
        }
        // Exact counts depend on selection/collisions. A twelve-note chord must
        // not create twelve schedulers; allow bounded selection-related variation.
        CHECK_MSG (totals[1] <= totals[0] * 2 + 8,
                   "12-note chord multiplied density: single=" + std::to_string (totals[0])
                   + ", chord=" + std::to_string (totals[1]));
    }
}

void testNoInput()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
        for (auto character : { Character::echo, Character::bloom, Character::pulse })
        {
            Render render ({}, settings (mode, character));
            render.until (30 * second);
            CHECK (render.output.empty());
            CHECK (render.status().generatedNoteOns == 0);
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
                     settings (OutputMode::add, Character::bloom, 0.0f));
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
    const auto control = settings (OutputMode::add, Character::echo);
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
                     settings (OutputMode::add, Character::bloom, 0.0f));
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
        Render render ({ note (0, true, 60, 110) }, settings (initial, Character::bloom, 0.0f));
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

void testZeroBlocks()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        const auto ordinary = renderForPartition (128, mode, Character::bloom);
        const auto withZeros = renderForPartition (128, mode, Character::bloom, true);
        sameTrace (ordinary, withZeros, "interspersed zero-length blocks must not consume or shift input");
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
        CHECK (onCount (render.output, 40 * second) == 0);
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

void testLongSilence()
{
    for (auto mode : { OutputMode::add, OutputMode::particles })
    {
        // Neither a physically held key nor a held pedal is an infinite activity source.
        Render render ({ cc (0, 64, 127), note (1, true, 60, 110) }, settings (mode));
        render.until (90 * second, 1024);
        const auto generated = mode == OutputMode::add ? generatedInAdd (render.output, render.input) : render.output;
        CHECK (onCount (generated) > 0);
        CHECK (onCount (generated, 60 * second) == 0);
        CHECK (render.status().generatedKeysDown == 0);
        balancedGenerated (generated);
        boundedRate (generated);
        if (mode == OutputMode::particles)
            CHECK (render.status().generatedHeld == 0);
    }
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
            controls.amount = block >= 200 && block < 210 ? 0.0f : 1.0f;
            controls.bypass = block >= 250 && block < 260;
            controls.character = block < 200 ? Character::echo : block < 400 ? Character::bloom : Character::pulse;
            if (block == 275)
                ++controls.resetSerial;
            if (block == 700)
                controls.outputMode = mode == OutputMode::add ? OutputMode::particles : OutputMode::add;

            {
                AllocationAudit::Scope probe;
                engine.process (sink.frames, transport, controls, source, sink);
            }
            const auto allocated = AllocationAudit::allocations;
            const auto freed = AllocationAudit::deallocations;
            CHECK_MSG (allocated == 0 && freed == 0, "process heap operations in block "
                       + std::to_string (block) + ": new=" + std::to_string (allocated)
                       + ", delete=" + std::to_string (freed));
            CHECK (sink.valid && source.cursor == source.count);
            // A block shorter than one second cannot produce more than 24 new
            // generated notes, their Offs, and the 24 already-reserved old Offs.
            CHECK (sink.count <= source.count + 3 * voiceBudget);
            messages += sink.count;
            const auto state = engine.getStatus();
            CHECK (state.generatedKeysDown + state.generatedHeld <= voiceBudget);
            sawGeneration = sawGeneration || state.generatedNoteOns != 0;
        }
        CHECK (messages > 0 && sawGeneration);
        CHECK (engine.getStatus().generatedKeysDown == 0 && engine.getStatus().generatedHeld == 0);
    }
}

struct TestCase
{
    std::string name;
    std::function<void()> run;
};

std::vector<TestCase> testCases()
{
    std::vector<TestCase> tests {
        { "add_amount_zero_byte_passthrough", testAddZero },
        { "particles_amount_zero_silent", testParticlesZero },
        { "character_echo_octaves_velocity", [] { testCharacter (Character::echo); } },
        { "character_bloom_octaves_velocity", [] { testCharacter (Character::bloom); } },
        { "character_pulse_octaves_velocity", [] { testCharacter (Character::pulse); } },
        { "amount_and_character_register_policy", testRegisterPolicy },
        { "dense_input_generated_on_off_balance", testDenseLifecycle },
        { "fixed_seed_reproducible", testSeedReproducibility },
        { "polyphonic_chord_global_rate", testChordRate },
        { "no_input_silent", testNoInput },
        { "amount_zero_interrupt_cancels_keys_and_candidates", testAmountInterrupt },
        { "bypass_cleanup_thru_and_resume", testBypass },
        { "stop_edge_then_frozen_transport_live_input", [] { testStopLive (false); } },
        { "stop_edge_same_block_live_input", [] { testStopLive (true); } },
        { "seek_cleans_old_epoch", [] { testDiscontinuity (false); } },
        { "loop_cleans_old_epoch", [] { testDiscontinuity (true); } },
        { "velocity_zero_off_and_repeated_key_counts", testVelocityZero },
        { "add_collision_retrigger_has_no_stale_off", testCollisionNoStaleOff },
        { "cc64_long_hold_budget_and_recovery", testSustainBudget },
        { "cc66_captures_only_down_edge_snapshot", testSostenutoSnapshot },
        { "cc64_cc66_intersection_release", testPedalIntersection },
        { "cc123_releases_keys_but_preserves_sustain", testAllNotesOff },
        { "cc120_cancels_activity_without_resetting_pedal", testAllSoundOff },
        { "cc121_releases_pedals_but_preserves_physical_keys", testResetControllers },
        { "particles_emergency_channel_messages_release_notes", testParticlesChannelMessages },
        { "output_mode_pending_notes_pedal_then_apply", testModePending },
        { "zero_length_does_not_consume_input", testZeroBlocks },
        { "reset_serial_hot_load_releases_and_cancels", testHotStateLoad },
        { "abnormal_tempo_bounded_and_finite", testAbnormalTempo },
        { "long_silence_ends_even_with_held_key_and_pedal", testLongSilence },
        { "sample_rates_and_missing_transport_fallback", testSampleRatesAndMissingClock },
        { "process_no_heap_allocation_and_bounded_messages", testProcessNoHeap }
    };
    for (const auto& character : std::array<std::pair<Character, const char*>, 3> {{
             { Character::echo, "echo" }, { Character::bloom, "bloom" }, { Character::pulse, "pulse" } }})
        for (auto mode : { OutputMode::add, OutputMode::particles })
        {
            const auto value = character.first;
            const std::string name = std::string ("block_partition_exact_32_64_128_512_1024_")
                                   + (mode == OutputMode::add ? "add_" : "particles_") + character.second;
            tests.push_back ({ name, [=] { testBlockPartitions (mode, value); } });
        }
    return tests;
}
} // namespace

int main (int argc, char** argv)
{
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    if (argc > 2)
    {
        std::cerr << "Usage: " << argv[0] << " [--list | case-name-substring]\n";
        return 2;
    }
    const std::string filter = argc == 2 ? argv[1] : "";
    const auto tests = testCases();
    if (filter == "--list")
    {
        for (const auto& test : tests)
            std::cout << test.name << '\n';
        return 0;
    }
    int passed = 0, failed = 0;
    for (const auto& test : tests)
    {
        if (! filter.empty() && test.name.find (filter) == std::string::npos)
            continue;
        std::cout << "[ RUN  ] " << test.name << '\n';
        try
        {
            test.run();
            ++passed;
            std::cout << "[ PASS ] " << test.name << '\n';
        }
        catch (const std::exception& error)
        {
            ++failed;
            std::cerr << "[ FAIL ] " << test.name << "\n         " << error.what() << '\n';
        }
        catch (...)
        {
            ++failed;
            std::cerr << "[ FAIL ] " << test.name << "\n         unknown exception\n";
        }
    }
    if (passed + failed == 0)
    {
        std::cerr << "No test matched '" << filter << "'. Use --list for case names.\n";
        return 2;
    }
    std::cout << "\n" << passed << " passed, " << failed << " failed, "
              << passed + failed << " total\n";
    return failed == 0 ? 0 : 1;
}
