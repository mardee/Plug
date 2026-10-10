#include "ParticleEngine.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace ozo::midi
{
ParticleEngine::Tick ParticleEngine::addTicks (Tick a, Tick b) noexcept
{
    return b > never - a ? never : a + b;
}

void ParticleEngine::increment (std::uint64_t& value) noexcept
{
    if (value != std::numeric_limits<std::uint64_t>::max())
        ++value;
}

void ParticleEngine::prepare (double sampleRate) noexcept
{
    sources_.fill (SourceKey {});
    channels_.fill (Channel {});
    tail_.fill (SourceChoice {});
    voices_.fill (Voice {});
    recentOns_.fill (0);
    activity_ = {};
    candidate_ = {};
    policy_ = mapAmount (0.30);
    tailSize_ = rateHead_ = rateSize_ = 0;
    lastSource_ = -1;
    sourceOrder_ = echoOrder_ = generatedNoteOns_ = skippedCandidates_ = 0;
    sampleRate_ = std::isfinite (sampleRate) && sampleRate >= 1.0 && sampleRate <= 768000.0
                    ? sampleRate : 44100.0;
    bpm_ = lastValidBpm_ = 120.0;
    beatsPerSample_ = 120.0L / (60.0L * sampleRate_);
    anchorBeat_ = 0.0L;
    anchorSample_ = sampleClock_ = 0;
    previousTransport_ = {};
    previousBlockSize_ = 0;
    transportSeen_ = previousPpqValid_ = false;
    amount_ = 0.30;
    character_ = Character::bloom;
    activeMode_ = requestedMode_ = OutputMode::add;
    seed_ = 0x6f7a6f31u;
    resetSerial_ = 0;
    enabled_ = controlsSeen_ = false;
    reseed();
}

ParticleEngine::Policy ParticleEngine::mapAmount (double amount) noexcept
{
    constexpr double positions[] { 0.0, 0.1, 0.3, 0.6, 1.0 };
    constexpr double rates[] { 0.25, 0.5, 1.0, 2.0, 4.0 };
    constexpr double spans[] { 0.5, 1.0, 2.0, 4.0, 8.0 };
    constexpr double velocities[] { 0.20, 0.30, 0.45, 0.50, 0.55 };
    int segment = 0;
    while (segment < 3 && amount > positions[segment + 1])
        ++segment;
    const double mix = (amount - positions[segment])
                       / (positions[segment + 1] - positions[segment]);
    Policy policy;
    policy.rate = rates[segment] + mix * (rates[segment + 1] - rates[segment]);
    policy.span = spans[segment] + mix * (spans[segment + 1] - spans[segment]);
    policy.velocity = velocities[segment]
                      + mix * (velocities[segment + 1] - velocities[segment]);
    policy.upperOctave = 0.50 * amount;
    policy.secondOctave = amount < 0.6 ? 0.0 : 0.04 + 0.40 * (amount - 0.6);
    policy.pulseStep = amount < 0.6 ? 0.5 : 0.25;
    return policy;
}

ParticleEngine::Tick ParticleEngine::durationSamples (double seconds) const noexcept
{
    return static_cast<Tick> (std::max (1.0L, std::ceil (static_cast<long double> (seconds)
                                                       * sampleRate_)));
}

long double ParticleEngine::beatAt (Tick time) const noexcept
{
    return anchorBeat_ + (static_cast<long double> (time)
                         - static_cast<long double> (anchorSample_)) * beatsPerSample_;
}

ParticleEngine::Tick ParticleEngine::sampleAtBeat (long double beat, Tick notBefore) const noexcept
{
    const long double target = static_cast<long double> (anchorSample_)
                               + (beat - anchorBeat_) / beatsPerSample_;
    if (target <= static_cast<long double> (notBefore))
        return notBefore;
    if (target >= static_cast<long double> (never))
        return never;
    // Snap only sub-micro-sample floating point error at exact musical boundaries.
    return std::max (notBefore, static_cast<Tick> (std::ceil (target - 1.0e-7L)));
}

bool ParticleEngine::updateClock (const Transport& transport, int numSamples) noexcept
{
    const bool validTempo = transport.hasTempo && std::isfinite (transport.bpm)
                            && transport.bpm >= 1.0 && transport.bpm <= 1000.0;
    if (validTempo)
        lastValidBpm_ = transport.bpm;
    const double newBpm = validTempo ? transport.bpm
                         : (transport.isPlaying ? 120.0 : lastValidBpm_);
    const bool ppqAvailable = transport.hasPpq && std::isfinite (transport.ppqPosition)
                              && std::abs (transport.ppqPosition) <= 1.0e9;
    const bool validPpq = transport.isPlaying && ppqAvailable;
    const long double currentBeat = beatAt (sampleClock_);
    bool discontinuity = transportSeen_
                         && transport.isPlaying != previousTransport_.isPlaying;

    if (transportSeen_ && transport.hasSamplePosition && previousTransport_.hasSamplePosition)
    {
        const long double delta = static_cast<long double> (transport.samplePosition)
                                  - previousTransport_.samplePosition;
        if (transport.isPlaying && previousTransport_.isPlaying)
            discontinuity = discontinuity || delta != previousBlockSize_;
        else if (! transport.isPlaying && ! previousTransport_.isPlaying)
            discontinuity = discontinuity || (delta != 0.0L && delta != previousBlockSize_);
    }

    if (transportSeen_ && validPpq && previousTransport_.isPlaying)
    {
        const long double expected = previousPpqValid_
            ? static_cast<long double> (previousTransport_.ppqPosition)
                + previousBlockSize_ * beatsPerSample_
            : currentBeat;
        const long double tolerance = std::max (1.0e-7L, 2.0L * beatsPerSample_);
        discontinuity = discontinuity
                        || std::abs (transport.ppqPosition - expected) > tolerance;
    }

    if (! transportSeen_ || discontinuity)
    {
        anchorSample_ = sampleClock_;
        anchorBeat_ = validPpq ? static_cast<long double> (transport.ppqPosition) : currentBeat;
    }
    else if (newBpm != bpm_)
    {
        anchorSample_ = sampleClock_;
        anchorBeat_ = currentBeat;
    }

    bpm_ = newBpm;
    beatsPerSample_ = static_cast<long double> (bpm_) / (60.0L * sampleRate_);
    previousTransport_ = transport;
    previousBlockSize_ = numSamples;
    previousPpqValid_ = validPpq;
    transportSeen_ = true;
    return discontinuity;
}

void ParticleEngine::reseed() noexcept
{
    randomState_ = seed_ == 0 ? 0x6f7a6f31u : seed_;
    echoOrder_ = 0;
    lastSource_ = -1;
}

double ParticleEngine::randomUnit() noexcept
{
    randomState_ ^= randomState_ << 13;
    randomState_ ^= randomState_ >> 17;
    randomState_ ^= randomState_ << 5;
    return static_cast<double> (randomState_) / 4294967296.0;
}

void ParticleEngine::cancelActivity() noexcept
{
    activity_ = {};
    candidate_ = {};
    tailSize_ = 0;
}

void ParticleEngine::cancelGeneration (Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    cancelActivity();
    for (auto& voice : voices_)
        releaseVoice (voice, time, blockStart, output);
}

double ParticleEngine::activityAt (Tick time) const noexcept
{
    if (! activity_.active || time >= activity_.hardEnd)
        return 0.0;
    const long double span = activity_.endBeat - activity_.startBeat;
    const long double left = activity_.endBeat - beatAt (time);
    if (span <= 0.0L || left <= 0.0L)
        return 0.0;
    return activity_.peak * static_cast<double> (std::min (1.0L, left / span));
}

void ParticleEngine::expireActivity (Tick time) noexcept
{
    if (activity_.active
        && (time >= activity_.hardEnd || sampleAtBeat (activity_.endBeat, time) == time))
        cancelActivity();
}

void ParticleEngine::strike (int velocity, Tick time) noexcept
{
    if (! enabled_)
        return;
    expireActivity (time);
    const double oldActivity = activityAt (time);
    activity_.peak = std::max (oldActivity, 0.65 + 0.35 * velocity / 127.0);
    activity_.startBeat = beatAt (time);
    activity_.endBeat = activity_.startBeat + policy_.span;
    activity_.hardEnd = addTicks (time, durationSamples (8.0));
    activity_.active = true;
    if (! candidate_.valid)
        scheduleCandidate (time, true);
}

void ParticleEngine::scheduleCandidate (Tick time, bool first) noexcept
{
    if (! activity_.active || ! enabled_)
    {
        candidate_.valid = false;
        return;
    }

    const long double now = beatAt (time);
    long double target = now;
    if (character_ == Character::pulse)
    {
        const long double step = policy_.pulseStep;
        target = (std::floor (now / step + 1.0e-12L) + 1.0L) * step;
    }
    else
    {
        double interval = 1.0 / policy_.rate;
        if (first)
            interval = std::min (0.5, interval);
        if (character_ == Character::bloom)
            interval *= 0.8 - 0.3 * amount_ + (0.4 + 0.6 * amount_) * randomUnit();
        target += interval;
    }
    candidate_.beat = target;
    candidate_.earliest = addTicks (time, 1);
    candidate_.valid = true;
}

ParticleEngine::Tick ParticleEngine::candidateTime (Tick from) const noexcept
{
    if (! candidate_.valid || ! activity_.active)
        return never;
    return sampleAtBeat (candidate_.beat, std::max (from, candidate_.earliest));
}

ParticleEngine::Tick ParticleEngine::nextScheduledTime (Tick from) const noexcept
{
    Tick next = candidateTime (from);
    if (activity_.active)
        next = std::min (next, std::min (sampleAtBeat (activity_.endBeat, from),
                                        std::max (from, activity_.hardEnd)));
    for (const auto& voice : voices_)
        if (voice.down)
            next = std::min (next, std::min (sampleAtBeat (voice.endBeat, from),
                                            std::max (from, voice.hardEnd)));
    return next;
}

void ParticleEngine::runBefore (Tick limit, Tick& cursor, Tick blockStart, MidiOutput& output) noexcept
{
    for (;;)
    {
        const Tick next = nextScheduledTime (cursor);
        if (next >= limit)
            break;
        runAt (next, blockStart, output);
        cursor = next;
    }
    cursor = limit;
}

void ParticleEngine::runAt (Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    releaseDue (time, blockStart, output);
    expireActivity (time);
    if (candidateTime (time) == time)
    {
        generate (time, blockStart, output);
        scheduleCandidate (time, false);
    }
    applyPendingMode();
}

bool ParticleEngine::poolChoice (int index, bool tail, bool physicalOnly,
                                SourceChoice& choice) const noexcept
{
    if (tail)
    {
        if (index >= tailSize_)
            return false;
        choice = tail_[static_cast<std::size_t> (index)];
        return true;
    }
    const auto& source = sources_[static_cast<std::size_t> (index)];
    if (! source.eligible || ! source.sounding || (physicalOnly && source.presses == 0))
        return false;
    choice = { index, source.velocity, source.lastStrike, source.order };
    return true;
}

bool ParticleEngine::hasSource() const noexcept
{
    if (tailSize_ != 0)
        return true;
    for (const auto& source : sources_)
        if (source.eligible && source.sounding)
            return true;
    return false;
}

ParticleEngine::SourceChoice ParticleEngine::selectSource (double draw, Tick time) noexcept
{
    bool physical = false;
    bool live = false;
    for (const auto& source : sources_)
    {
        if (source.eligible && source.sounding)
        {
            live = true;
            physical = physical || source.presses != 0;
        }
    }
    const bool tail = ! live;
    const int count = tail ? tailSize_ : keyCount;
    SourceChoice choice;
    SourceChoice result;
    if (character_ == Character::echo)
    {
        SourceChoice first;
        for (int index = 0; index < count; ++index)
        {
            if (! poolChoice (index, tail, physical, choice))
                continue;
            if (first.key < 0 || choice.order < first.order)
                first = choice;
            if (choice.order > echoOrder_ && (result.key < 0 || choice.order < result.order))
                result = choice;
        }
        if (result.key < 0)
            result = first;
        if (result.key >= 0)
            echoOrder_ = result.order;
        return result;
    }

    int available = 0;
    for (int index = 0; index < count; ++index)
        if (poolChoice (index, tail, physical, choice))
            ++available;

    const auto weight = [this, time, available] (const SourceChoice& source) noexcept
    {
        if (character_ == Character::bloom && available > 1 && source.key == lastSource_)
            return 0.0;
        const double age = static_cast<double> (time - source.lastStrike) / sampleRate_;
        return 1.0 + source.velocity / 127.0 + 2.0 / (1.0 + age);
    };
    double total = 0.0;
    for (int index = 0; index < count; ++index)
        if (poolChoice (index, tail, physical, choice))
            total += weight (choice);
    double target = draw * total;
    for (int index = 0; index < count; ++index)
    {
        if (! poolChoice (index, tail, physical, choice))
            continue;
        const double current = weight (choice);
        if (current <= 0.0)
            continue;
        result = choice;
        if (target < current)
            break;
        target -= current;
    }
    return result;
}

bool ParticleEngine::keyAvailable (int key) const noexcept
{
    if (key < 0 || key >= keyCount)
        return false;
    const auto& source = sources_[static_cast<std::size_t> (key)];
    if (activeMode_ == OutputMode::add && (source.presses != 0 || source.sounding))
        return false;
    for (const auto& voice : voices_)
        if (voice.key == key)
            return false;
    return true;
}

bool ParticleEngine::rateAvailable (Tick time) noexcept
{
    const Tick window = durationSamples (1.0);
    while (rateSize_ > 0 && time - recentOns_[static_cast<std::size_t> (rateHead_)] >= window)
    {
        rateHead_ = (rateHead_ + 1) % voiceCount;
        --rateSize_;
    }
    return rateSize_ < voiceCount;
}

void ParticleEngine::recordNoteOn (Tick time) noexcept
{
    recentOns_[static_cast<std::size_t> ((rateHead_ + rateSize_) % voiceCount)] = time;
    ++rateSize_;
    increment (generatedNoteOns_);
}

void ParticleEngine::generate (Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    const double activity = activityAt (time);
    const double chance = activity * (character_ == Character::pulse
                                      ? std::min (1.0, policy_.rate * policy_.pulseStep) : 1.0);
    const double trigger = randomUnit();
    if (! hasSource())
    {
        cancelActivity();
        increment (skippedCandidates_);
        return;
    }
    if (trigger >= chance || ! rateAvailable (time))
    {
        increment (skippedCandidates_);
        return;
    }

    Voice* freeVoice = nullptr;
    for (auto& voice : voices_)
        if (voice.key < 0)
        {
            freeVoice = &voice;
            break;
        }
    if (freeVoice == nullptr)
    {
        increment (skippedCandidates_);
        return;
    }

    SourceChoice source;
    int key = -1;
    for (int attempt = 0; attempt < 4 && key < 0; ++attempt)
    {
        source = selectSource (randomUnit(), time);
        if (source.key < 0)
            break;
        const double second = character_ == Character::echo ? 0.0 : policy_.secondOctave;
        const double octaveDraw = randomUnit();
        const int preferred = octaveDraw < second ? 24
                              : octaveDraw < second + policy_.upperOctave ? 12 : 0;
        const int octaves[] { preferred, preferred == 0 ? 12 : 0,
                             preferred == 24 ? 12 : 24 };
        const int octaveCount = second > 0.0 ? 3 : 2;
        for (int alternative = 0; alternative < octaveCount; ++alternative)
        {
            const int pitch = source.key % 128 + octaves[alternative];
            const int candidate = source.key / 128 * 128 + pitch;
            if (pitch < 128 && keyAvailable (candidate))
            {
                key = candidate;
                break;
            }
        }
    }
    if (key < 0)
    {
        increment (skippedCandidates_);
        return;
    }

    const double strength = policy_.velocity * (0.75 + 0.5 * randomUnit())
                            * (0.45 + 0.55 * activity);
    const int velocity = std::clamp (static_cast<int> (std::lround (source.velocity * strength)),
                                     1, 127);
    const double gate = 0.125 + 0.375 * randomUnit();
    freeVoice->key = key;
    freeVoice->down = true;
    freeVoice->sostenuto = false;
    freeVoice->endBeat = beatAt (time) + gate;
    freeVoice->hardEnd = addTicks (time, durationSamples (2.0));
    lastSource_ = source.key;
    emitNote (static_cast<int> (time - blockStart), key, true, velocity, output);
    recordNoteOn (time);
}

void ParticleEngine::rememberTail (int key) noexcept
{
    const auto& source = sources_[static_cast<std::size_t> (key)];
    if (! activity_.active || ! source.eligible)
        return;
    const SourceChoice choice { key, source.velocity, source.lastStrike, source.order };
    for (int index = 0; index < tailSize_; ++index)
        if (tail_[static_cast<std::size_t> (index)].key == key)
        {
            tail_[static_cast<std::size_t> (index)] = choice;
            return;
        }
    if (tailSize_ < tailCapacity)
    {
        tail_[static_cast<std::size_t> (tailSize_++)] = choice;
        return;
    }
    int oldest = 0;
    for (int index = 1; index < tailSize_; ++index)
        if (tail_[static_cast<std::size_t> (index)].order < tail_[static_cast<std::size_t> (oldest)].order)
            oldest = index;
    if (choice.order > tail_[static_cast<std::size_t> (oldest)].order)
        tail_[static_cast<std::size_t> (oldest)] = choice;
}

void ParticleEngine::removeChannelTail (int channel) noexcept
{
    int kept = 0;
    for (int index = 0; index < tailSize_; ++index)
        if (tail_[static_cast<std::size_t> (index)].key / 128 != channel)
            tail_[static_cast<std::size_t> (kept++)] = tail_[static_cast<std::size_t> (index)];
    tailSize_ = kept;
}

void ParticleEngine::refreshSources (int channel) noexcept
{
    const auto& pedal = channels_[static_cast<std::size_t> (channel)];
    for (int pitch = 0; pitch < 128; ++pitch)
    {
        const int key = channel * 128 + pitch;
        auto& source = sources_[static_cast<std::size_t> (key)];
        if (source.sounding && source.presses == 0
            && ! pedal.sustain && ! (pedal.sostenuto && source.sostenuto))
        {
            rememberTail (key);
            source.sounding = source.sostenuto = source.eligible = false;
        }
    }
}

void ParticleEngine::emitNote (int offset, int key, bool on, int velocity, MidiOutput& output) noexcept
{
    const std::uint8_t bytes[] {
        static_cast<std::uint8_t> ((on ? 0x90 : 0x80) | (key / 128)),
        static_cast<std::uint8_t> (key % 128),
        static_cast<std::uint8_t> (on ? velocity : 0)
    };
    output.push (offset, bytes, 3);
}

void ParticleEngine::releaseVoice (Voice& voice, Tick time, Tick blockStart,
                                  MidiOutput& output, bool emit) noexcept
{
    if (! voice.down)
        return;
    if (emit)
        emitNote (static_cast<int> (time - blockStart), voice.key, false, 0, output);
    voice.down = false;
    const auto& pedal = channels_[static_cast<std::size_t> (voice.key / 128)];
    if (activeMode_ != OutputMode::add
        || (! pedal.sustain && ! (pedal.sostenuto && voice.sostenuto)))
        voice = {};
}

void ParticleEngine::releaseDue (Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    for (auto& voice : voices_)
        if (voice.down && (voice.hardEnd <= time || sampleAtBeat (voice.endBeat, time) == time))
            releaseVoice (voice, time, blockStart, output);
}

void ParticleEngine::refreshVoices (int channel) noexcept
{
    const auto& pedal = channels_[static_cast<std::size_t> (channel)];
    for (auto& voice : voices_)
        if (voice.key >= 0 && voice.key / 128 == channel && ! voice.down
            && (! pedal.sustain && ! (pedal.sostenuto && voice.sostenuto)))
            voice = {};
}

void ParticleEngine::noteOn (int key, int velocity, Tick time, Tick blockStart,
                            MidiOutput& output) noexcept
{
    if (activeMode_ == OutputMode::add)
        for (auto& voice : voices_)
            if (voice.key == key && voice.down)
                releaseVoice (voice, time, blockStart, output);
    auto& source = sources_[static_cast<std::size_t> (key)];
    increment (source.presses);
    increment (sourceOrder_);
    source.order = sourceOrder_;
    source.lastStrike = time;
    source.velocity = static_cast<std::uint8_t> (velocity);
    source.sounding = source.eligible = true;
    tailSize_ = 0;
    strike (velocity, time);
}

void ParticleEngine::noteOff (int key, Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    auto& source = sources_[static_cast<std::size_t> (key)];
    if (source.presses > 0)
        --source.presses;
    refreshSources (key / 128);
    // A raw Off already releases this MIDI 1.0 key; retire its scheduled Off as well.
    if (activeMode_ == OutputMode::add)
        for (auto& voice : voices_)
            if (voice.key == key && voice.down)
                releaseVoice (voice, time, blockStart, output, false);
}

void ParticleEngine::controller (int channel, int cc, int value, Tick time,
                                 Tick blockStart, MidiOutput& output) noexcept
{
    auto& pedal = channels_[static_cast<std::size_t> (channel)];
    if (cc == 64)
    {
        pedal.sustain = value >= 64;
        refreshSources (channel);
        refreshVoices (channel);
    }
    else if (cc == 66)
    {
        const bool down = value >= 64;
        if (down != pedal.sostenuto)
        {
            pedal.sostenuto = down;
            for (int pitch = 0; pitch < 128; ++pitch)
            {
                auto& source = sources_[static_cast<std::size_t> (channel * 128 + pitch)];
                source.sostenuto = down && source.presses != 0 && source.sounding;
            }
            for (auto& voice : voices_)
                if (voice.key >= 0 && voice.key / 128 == channel)
                    voice.sostenuto = down && voice.down && activeMode_ == OutputMode::add;
        }
        refreshSources (channel);
        refreshVoices (channel);
    }
    else if (cc == 121)
    {
        pedal = {};
        for (int pitch = 0; pitch < 128; ++pitch)
            sources_[static_cast<std::size_t> (channel * 128 + pitch)].sostenuto = false;
        for (auto& voice : voices_)
            if (voice.key >= 0 && voice.key / 128 == channel)
                voice.sostenuto = false;
        refreshSources (channel);
        refreshVoices (channel);
    }
    else if (cc == 120 || cc == 123)
    {
        for (int pitch = 0; pitch < 128; ++pitch)
        {
            auto& source = sources_[static_cast<std::size_t> (channel * 128 + pitch)];
            source.eligible = false;
            if (cc == 120)
                source.sounding = source.sostenuto = false;
            else
                source.presses = 0;
        }
        removeChannelTail (channel);
        refreshSources (channel);
        for (auto& voice : voices_)
        {
            if (voice.key < 0 || voice.key / 128 != channel)
                continue;
            releaseVoice (voice, time, blockStart, output);
            if (cc == 120)
                voice = {};
        }
        if (! hasSource())
            cancelActivity();
    }
}

void ParticleEngine::handleInput (const MidiEventView& event, Tick time, Tick blockStart,
                                  MidiOutput& output) noexcept
{
    if (event.data == nullptr || event.size <= 0)
        return;
    const auto* bytes = event.data;
    if (event.size >= 3 && bytes[0] >= 0x80 && bytes[0] < 0xf0
        && bytes[1] < 128 && bytes[2] < 128)
    {
        const int channel = bytes[0] & 0x0f;
        const int message = bytes[0] & 0xf0;
        const int key = channel * 128 + bytes[1];
        if (message == 0x90 && bytes[2] != 0)
            noteOn (key, bytes[2], time, blockStart, output);
        else if (message == 0x80 || (message == 0x90 && bytes[2] == 0))
            noteOff (key, time, blockStart, output);
        else if (message == 0xb0)
            controller (channel, bytes[1], bytes[2], time, blockStart, output);
    }
    if (activeMode_ == OutputMode::add)
        output.push (event.sampleOffset, event.data, event.size);
}

void ParticleEngine::applyPendingMode() noexcept
{
    if (requestedMode_ == activeMode_ || activity_.active)
        return;
    for (const auto& source : sources_)
        if (source.presses != 0 || source.sounding)
            return;
    for (const auto& pedal : channels_)
        if (pedal.sustain || pedal.sostenuto)
            return;
    for (const auto& voice : voices_)
        if (voice.key >= 0)
            return;
    activeMode_ = requestedMode_;
}

void ParticleEngine::process (int numSamples, const Transport& transport, const Controls& controls,
                              MidiInput& input, MidiOutput& output) noexcept
{
    if (numSamples <= 0)
        return;

    const Tick blockStart = sampleClock_;
    const Tick blockEnd = addTicks (blockStart, static_cast<Tick> (numSamples));
    const bool newEpoch = updateClock (transport, numSamples);
    const bool reset = controlsSeen_
                       && (controls.resetSerial != resetSerial_ || controls.seed != seed_);
    const Character newCharacter = controls.character == Character::echo ? Character::echo
                                   : controls.character == Character::pulse ? Character::pulse
                                                                           : Character::bloom;
    const bool characterChanged = controlsSeen_ && newCharacter != character_;
    amount_ = std::isfinite (controls.amount) ? std::clamp (static_cast<double> (controls.amount),
                                                           0.0, 1.0) : 0.0;
    policy_ = mapAmount (amount_);
    character_ = newCharacter;
    requestedMode_ = controls.outputMode == OutputMode::particles ? OutputMode::particles
                                                                 : OutputMode::add;
    const bool newEnabled = amount_ > 0.0 && ! controls.bypass;
    if (newEpoch || reset || (enabled_ && ! newEnabled))
        cancelGeneration (blockStart, blockStart, output);
    seed_ = controls.seed;
    resetSerial_ = controls.resetSerial;
    if (! controlsSeen_ || newEpoch || reset)
        reseed();
    controlsSeen_ = true;
    enabled_ = newEnabled;
    if (characterChanged && activity_.active)
        scheduleCandidate (blockStart, true);
    applyPendingMode();

    Tick cursor = blockStart;
    MidiEventView event;
    bool haveEvent = input.next (event);
    while (haveEvent)
    {
        // The adapter owns out-of-block events; no borrowed pointer is kept across calls.
        if (event.sampleOffset < 0 || event.sampleOffset >= numSamples)
        {
            haveEvent = input.next (event);
            continue;
        }
        const int offset = event.sampleOffset;
        const Tick time = addTicks (blockStart, static_cast<Tick> (offset));
        runBefore (time, cursor, blockStart, output);
        do
        {
            handleInput (event, time, blockStart, output);
            haveEvent = input.next (event);
        }
        while (haveEvent && event.sampleOffset == offset);
        runAt (time, blockStart, output);
        cursor = time;
    }
    runBefore (blockEnd, cursor, blockStart, output);
    sampleClock_ = blockEnd;
}

EngineStatus ParticleEngine::getStatus() const noexcept
{
    EngineStatus status;
    status.activeMode = activeMode_;
    status.modeChangePending = activeMode_ != requestedMode_;
    status.generatedNoteOns = generatedNoteOns_;
    status.skippedCandidates = skippedCandidates_;
    for (const auto& voice : voices_)
    {
        if (voice.down)
            ++status.generatedKeysDown;
        else if (voice.key >= 0)
            ++status.generatedHeld;
    }
    return status;
}
}
