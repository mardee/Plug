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
    voices_.fill (Voice {});
    recentOns_.fill (0);
    clouds_.fill (Cloud {});
    priorityUntil_ = nextEmit_ = 0;
    rateHead_ = rateSize_ = 0;
    cloudOrder_ = generatedNoteOns_ = skippedCandidates_ = 0;
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
    manualLanes_ = false;
    manualControls_ = defaultLanes();
    root_ = 0;
    scale_ = Scale::unset;
    activeMode_ = requestedMode_ = OutputMode::add;
    seed_ = randomState_ = 0x6f7a6f31u;
    resetSerial_ = 0;
    enabled_ = controlsSeen_ = false;
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
    const double newBpm = validTempo ? transport.bpm : lastValidBpm_;
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
    else if (validPpq)
    {
        // Sync grids follow the host's beat origin, including tempo automation.
        anchorSample_ = sampleClock_;
        anchorBeat_ = static_cast<long double> (transport.ppqPosition);
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

double ParticleEngine::randomUnit (std::uint32_t& state) noexcept
{
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return static_cast<double> (state) / 4294967296.0;
}

void ParticleEngine::cancelClouds() noexcept
{
    clouds_.fill (Cloud {});
    priorityUntil_ = 0;
}

void ParticleEngine::cancelGeneration (Tick time, Tick blockStart, MidiOutput& output) noexcept
{
    cancelClouds();
    for (auto& voice : voices_)
        releaseVoice (voice, time, blockStart, output);
    // Pedal-held keys retain reservations until the matching incoming pedal release.
    // Never send a channel-wide pedal/All-Sound-Off that could kill the melody.
}

void ParticleEngine::strike (int key, int velocity, Tick time) noexcept
{
    if (! enabled_)
        return;
    priorityUntil_ = addTicks (time, durationSamples (0.035));
    Cloud* slot = nullptr;
    Cloud* oldest = nullptr;
    Cloud* oldestChannel = nullptr;
    Cloud* chord = nullptr;
    int channelCount = 0;
    for (auto& cloud : clouds_)
    {
        if (cloud.activeLanes != 0
            && (time >= cloud.hardEnd
                || beatAt (time) >= cloud.triggerBeat + (cloud.manual ? 64.0L : 32.0L)))
            cloud = {};
        if (cloud.activeLanes == 0)
        {
            if (slot == nullptr)
                slot = &cloud;
            continue;
        }
        if (oldest == nullptr || cloud.order < oldest->order)
            oldest = &cloud;
        if (cloud.notes[0].key / 128 != key / 128)
            continue;
        ++channelCount;
        if (oldestChannel == nullptr || cloud.order < oldestChannel->order)
            oldestChannel = &cloud;
        // Chord window is anchored to its first strike, never extended by arpeggios.
        if (cloud.step == 0 && time - cloud.triggerTime <= durationSamples (0.020))
            chord = &cloud;
    }
    if (chord != nullptr)
    {
        for (int i = 0; i < chord->noteCount; ++i)
            if (chord->notes[static_cast<std::size_t> (i)].key == key)
            {
                chord->notes[static_cast<std::size_t> (i)].velocity = velocity;
                return;
            }
        if (chord->noteCount < 8)
            chord->notes[static_cast<std::size_t> (chord->noteCount++)] = { key, velocity };
        return;
    }
    // New harmony cannot be guaranteed consonant with an old cloud: age plus each
    // subsequent input cloud attenuates it, without resetting its decay clock.
    for (auto& cloud : clouds_)
        if (cloud.activeLanes != 0 && ! cloud.manual)
            cloud.newerStrikes = std::min (64, cloud.newerStrikes + 1);
    if (channelCount >= 3)
        slot = oldestChannel;
    if (slot == nullptr)
        slot = oldest;
    if (slot == nullptr)
        return;
    // Retiring a full slot only forgets future replies, never its emitted Off ledger.
    *slot = {};
    increment (cloudOrder_);
    slot->order = cloudOrder_;
    slot->notes[0] = { key, velocity };
    slot->noteCount = 1;
    slot->triggerTime = time;
    slot->triggerBeat = beatAt (time);
    // Safety guards only; the relative exponential upper bound normally ends lanes.
    slot->manual = manualLanes_;
    slot->hardEnd = addTicks (time, durationSamples (slot->manual ? 48.0 : 24.0));
    constexpr IntervalRole roles[] { IntervalRole::unison, IntervalRole::upThird,
        IntervalRole::upFifth, IntervalRole::upOctave, IntervalRole::downThird,
        IntervalRole::downFifth, IntervalRole::downOctave };
    // Additional lower replies enter later; all offsets are finite musical values.
    constexpr long double firstOffsets[] { 1.0L, 0.75L, 1.5L, 0.5L, 2.0L, 2.5L, 3.0L };
    constexpr double thresholds[] { 0.65, 0.18, 0.28, 0.0, 0.75, 0.75, 0.50 };
    for (std::size_t i = 0; i < slot->lanes.size(); ++i)
    {
        auto& lane = slot->lanes[i];
        lane.intervalRole = roles[i];
        randomUnit (randomState_);
        lane.randomState = randomState_;
        // Reserved former count draw preserves the initial gain/decay random sequence.
        randomUnit (lane.randomState);
        const double gainDraw = randomUnit (lane.randomState);
        lane.firstGain = i == 0 ? 0.22 : i >= 4 ? 0.25 + 0.07 * gainDraw
                                                               : 0.32 + 0.13 * gainDraw;
        lane.decay = 0.14 + 0.06 * randomUnit (lane.randomState);
        // Independent waves, staggered by role; neither velocity nor Amount enters.
        lane.phase = static_cast<double> (i) * 0.897597901025655
                     + 6.283185307179586 * randomUnit (lane.randomState);
        lane.wavePeriod = 2.7 + 0.6 * randomUnit (lane.randomState);
        const bool needsScale = i == 1 || i == 2 || i == 4 || i == 5;
        lane.settings = manualControls_[i];
        lane.active = slot->manual ? lane.settings.enabled && lane.settings.level > 0.0f
            : amount_ + 1.0e-6 >= thresholds[i] && (! needsScale || scale_ != Scale::unset);
        if (lane.active)
            ++slot->activeLanes;
        // Decay starts at the planned first reply, even while its key is occupied.
        lane.firstBeat = slot->triggerBeat + (slot->manual
            ? static_cast<long double> (noteBeats[static_cast<std::size_t> (lane.settings.first)])
            : firstOffsets[i]);
        lane.nextBeat = lane.firstBeat;
        lane.earliest = addTicks (time, slot->manual ? Tick { 1 } : durationSamples (0.060));
        // Each candidate yields to new input for at most 120 ms, then grid-snaps.
        lane.priorityCap = addTicks (sampleAtBeat (lane.nextBeat, lane.earliest),
                                     durationSamples (0.120));
    }
}

long double ParticleEngine::nextGap (Lane& lane) noexcept
{
    const double draw = randomUnit (lane.randomState);
    // Quarter-note beats, including dotted values. Each role has its own clock;
    // only the fifth occasionally makes a pair, never an unbounded short run.
    if (lane.intervalRole == IntervalRole::upThird || lane.intervalRole == IntervalRole::downThird)
        return draw < 0.55 ? 1.0L : 1.5L;
    if (lane.intervalRole == IntervalRole::upOctave || lane.intervalRole == IntervalRole::downOctave
        || lane.intervalRole == IntervalRole::unison)
        return draw < 0.25 ? 0.75L : draw < 0.65 ? 1.0L : 1.5L;
    if (lane.shortRun != 0)
    {
        lane.shortRun = 0;
        return draw < 0.5 ? 0.75L : 1.0L;
    }
    const long double gap = amount_ > 0.65
        ? (draw < 0.04 ? 0.125L : draw < 0.10 ? 0.25L : draw < 0.18 ? 0.375L
           : draw < 0.50 ? 0.5L : draw < 0.80 ? 0.75L : 1.0L)
        : (draw < 0.06 ? 0.25L : draw < 0.12 ? 0.375L
           : draw < 0.45 ? 0.5L : draw < 0.80 ? 0.75L : 1.0L);
    lane.shortRun = gap < 0.5L ? 1 : 0;
    return gap;
}

ParticleEngine::Tick ParticleEngine::cloudTime (const Cloud& cloud, const Lane& lane,
                                                Tick from) const noexcept
{
    if (! lane.active)
        return never;
    const Tick expiry = std::min (std::max (from, cloud.hardEnd),
                                 sampleAtBeat (cloud.triggerBeat + (cloud.manual ? 64.0L : 32.0L), from));
    const Tick priority = cloud.manual ? Tick { 0 } : std::min (priorityUntil_, lane.priorityCap);
    const Tick earliest = std::max ({ from, lane.earliest, priority, nextEmit_ });
    const long double grid = cloud.manual ? 0.0625L : 0.125L;
    const Tick planned = sampleAtBeat (lane.nextBeat, 0);
    const long double needed = beatAt (earliest);
    // Preserve the 0.2.1 sample-aware ceil: at non-integral tempos a rounded
    // sample boundary must not accidentally defer the candidate an extra cell.
    const long double cells = planned >= earliest ? 0.0L
        : std::max (0.0L, std::ceil ((needed - lane.nextBeat - beatsPerSample_) / grid - 1.0e-7L));
    const Tick due = sampleAtBeat (lane.nextBeat + cells * grid, earliest);
    return std::min (expiry, due);
}

ParticleEngine::Tick ParticleEngine::voiceTime (const Voice& voice, Tick from) const noexcept
{
    if (! voice.down)
        return never;
    return std::min (std::max (from, voice.hardEnd),
                    sampleAtBeat (voice.endBeat, std::max (from, voice.earliestEnd)));
}

ParticleEngine::Tick ParticleEngine::nextScheduledTime (Tick from) const noexcept
{
    Tick next = never;
    for (const auto& cloud : clouds_)
        for (const auto& lane : cloud.lanes)
            next = std::min (next, cloudTime (cloud, lane, from));
    for (const auto& voice : voices_)
        next = std::min (next, voiceTime (voice, from));
    return next;
}

void ParticleEngine::runBefore (Tick limit, Tick& cursor, Tick blockStart, MidiOutput& output,
                                GeneratedNoteObserver* observer) noexcept
{
    for (;;)
    {
        const Tick next = nextScheduledTime (cursor);
        if (next >= limit)
            break;
        runAt (next, blockStart, output, observer);
        cursor = next;
    }
    cursor = limit;
}

void ParticleEngine::runAt (Tick time, Tick blockStart, MidiOutput& output,
                            GeneratedNoteObserver* observer) noexcept
{
    releaseDue (time, blockStart, output);
    // Bounded coordinator: oldest scheduled sample wins, then cloud order, then
    // lane array order. Successful Ons stagger all other lanes on their own grids.
    for (int pass = 0; pass < 56; ++pass)
    {
        Cloud* due = nullptr;
        Lane* dueLane = nullptr;
        Tick dueSample = never;
        for (auto& cloud : clouds_)
        {
            if (cloud.activeLanes == 0)
                continue;
            if (time >= cloud.hardEnd
                || sampleAtBeat (cloud.triggerBeat + (cloud.manual ? 64.0L : 32.0L), time) == time)
            {
                cloud = {};
                continue;
            }
            for (auto& lane : cloud.lanes)
                if (cloudTime (cloud, lane, time) == time)
                {
                    // Compare sample positions, not floating PPQ, for stable ties.
                    const Tick scheduled = sampleAtBeat (lane.nextBeat, 0);
                    if (due == nullptr || scheduled < dueSample
                        || (scheduled == dueSample && cloud.order < due->order))
                    {
                        due = &cloud;
                        dueLane = &lane;
                        dueSample = scheduled;
                    }
                }
        }
        if (due == nullptr)
            break;
        const long double onsetBeat = onsetAt (*due, beatAt (time));
        // End on the upper envelope, not a wave trough or absolute MIDI velocity.
        // This only retires future replies; sounding/held voices keep their ledger.
        const auto& live = manualControls_[static_cast<std::size_t> (dueLane->intervalRole)];
        const bool onePass = due->manual && live.feedback == 0.0f && dueLane->emitted == 0;
        const double upper = decayUpper (*due, *dueLane, onsetBeat);
        if (upper <= 0.0 || (! onePass && upper <= 0.035))
        {
            dueLane->active = false;
            --due->activeLanes;
            continue;
        }
        if (! rateAvailable (time))
        {
            dueLane->earliest = addTicks (recentOns_[static_cast<std::size_t> (rateHead_)],
                                         durationSamples (1.0));
            continue;
        }
        if (generate (*due, *dueLane, time, blockStart, output, observer))
        {
            ++due->step;
            ++dueLane->emitted;
            if (due->manual && live.feedback == 0.0f)
            {
                dueLane->active = false;
                --due->activeLanes;
                continue;
            }
        }
        // Manual repeats retain their input-snapshot clock, skipping missed ticks
        // rather than accumulating debt or drifting after a scheduling collision.
        if (due->manual)
        {
            const long double repeat = noteBeats[static_cast<std::size_t> (dueLane->settings.repeat)];
            const long double steps = std::max (1.0L,
                std::floor ((onsetBeat - dueLane->nextBeat) / repeat) + 1.0L);
            dueLane->nextBeat += steps * repeat;
        }
        else
            dueLane->nextBeat = onsetBeat + nextGap (*dueLane);
        dueLane->earliest = addTicks (time, 1);
        dueLane->priorityCap = addTicks (sampleAtBeat (dueLane->nextBeat, dueLane->earliest),
                                        durationSamples (0.120));
    }
    applyPendingMode();
}

int ParticleEngine::scaleDegree (int pitch) const noexcept
{
    constexpr int major[] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int minor[] { 0, 2, 3, 5, 7, 8, 10 };
    if (scale_ == Scale::unset)
        return -1;
    const int* intervals = scale_ == Scale::minor ? minor : major;
    const int relative = (pitch - root_ + 12) % 12;
    for (int d = 0; d < 7; ++d)
        if (intervals[d] == relative)
            return d;
    return -1;
}

int ParticleEngine::selectPitch (const Cloud& cloud, Lane& lane, SourceNote& source) noexcept
{
    struct Choice { int key = -1; int sourceIndex = 0; double weight = 0.0; };
    std::array<Choice, 8> choices {};
    int count = 0;
    double total = 0.0;
    constexpr int major[] { 0, 2, 4, 5, 7, 9, 11 };
    constexpr int minor[] { 0, 2, 3, 5, 7, 8, 10 };
    const int* intervals = scale_ == Scale::minor ? minor : major;
    for (int n = 0; n < cloud.noteCount; ++n)
    {
        const auto& note = cloud.notes[static_cast<std::size_t> (n)];
        const int pitch = note.key % 128;
        const int degree = scaleDegree (pitch);
        const bool third = lane.intervalRole == IntervalRole::upThird
                           || lane.intervalRole == IntervalRole::downThird;
        const bool fifth = lane.intervalRole == IntervalRole::upFifth
                           || lane.intervalRole == IntervalRole::downFifth;
        const bool downward = lane.intervalRole == IntervalRole::downThird
                              || lane.intervalRole == IntervalRole::downFifth
                              || lane.intervalRole == IntervalRole::downOctave;
        if ((third || fifth) && degree < 0)
            continue; // Never turn an unavailable harmony lane into another octave.
        int offset = 0;
        if (third || fifth)
        {
            const int steps = (third ? 2 : 4) * (downward ? -1 : 1);
            // Shift by a scale octave before division so negative degrees floor.
            const int target = degree + steps + 7;
            offset = intervals[target % 7] + 12 * (target / 7 - 1) - intervals[degree];
        }
        else if (lane.intervalRole == IntervalRole::upOctave)
            offset = 12;
        else if (lane.intervalRole == IntervalRole::downOctave)
            offset = -12;
        // Exactly one interval per lane/source; unavailable keys never substitute
        // another register or role. Chord-member voice leading remains independent.
        const int target = pitch + offset;
        const int key = note.key / 128 * 128 + target;
        if (target > 127 || target < 0 || (downward && target < 48) || ! keyAvailable (key))
            continue;
        double weight = 1.0;
        // Soft voice leading across ALL chord members, never highest-note-first.
        if (lane.lastPitch >= 0)
        {
            weight *= 0.70 + 0.30 / (1.0 + std::abs (target - lane.lastPitch) / 7.0);
            if (target == lane.lastPitch)
                weight *= 0.35;
        }
        if (cloud.noteCount > 1 && n == lane.lastSource)
            weight *= 0.60;
        choices[static_cast<std::size_t> (count++)] = { key, n, weight };
        total += weight;
    }
    if (count == 0)
        return -1;
    double draw = randomUnit (lane.randomState) * total;
    for (int i = 0; i < count; ++i)
    {
        const auto& choice = choices[static_cast<std::size_t> (i)];
        if (draw < choice.weight || i == count - 1)
        {
            source = cloud.notes[static_cast<std::size_t> (choice.sourceIndex)];
            lane.lastSource = choice.sourceIndex;
            lane.lastPitch = choice.key % 128;
            return choice.key;
        }
        draw -= choice.weight;
    }
    return -1;
}

long double ParticleEngine::onsetAt (const Cloud& cloud, long double beat) noexcept
{
    // On and Off share the same source-relative grid. Round the sample back to
    // its musical endpoint; ceil here would add a cell at tempos such as 173 BPM.
    const long double cellsPerBeat = cloud.manual ? 16.0L : 8.0L;
    return cloud.triggerBeat
        + std::round ((beat - cloud.triggerBeat) * cellsPerBeat) / cellsPerBeat;
}

void ParticleEngine::updateManualControls (const Controls& controls) noexcept
{
    for (std::size_t i = 0; i < manualControls_.size(); ++i)
    {
        auto value = controls.lanes[i];
        const int lastNote = static_cast<int> (noteBeats.size()) - 1;
        value.first = std::clamp (value.first, 0, lastNote);
        value.repeat = std::clamp (value.repeat, 0, lastNote);
        value.gate = std::clamp (value.gate, 0, lastNote);
        value.cycle = std::clamp (value.cycle, 0, 3);
        value.level = std::isfinite (value.level) ? std::clamp (value.level, 0.0f, 1.0f) : 0.0f;
        value.feedback = std::isfinite (value.feedback)
            ? std::clamp (value.feedback, 0.0f, 0.98f) : 0.0f;
        value.motion = std::isfinite (value.motion) ? std::clamp (value.motion, 0.0f, 1.0f) : 0.0f;
        manualControls_[i] = value;
        // Disable is destructive only for future replies. Re-enable cannot revive
        // old lanes, and the independent voice ledger keeps every outstanding Off.
        if (! value.enabled || value.level == 0.0f)
            for (auto& cloud : clouds_)
                if (cloud.manual && cloud.lanes[i].active)
                {
                    cloud.lanes[i].active = false;
                    --cloud.activeLanes;
                }
    }
}

double ParticleEngine::decayUpper (const Cloud& cloud, const Lane& lane,
                                  long double onsetBeat) const noexcept
{
    const double elapsed = static_cast<double> (std::max (0.0L, onsetBeat - lane.firstBeat));
    if (cloud.manual)
    {
        const auto& live = manualControls_[static_cast<std::size_t> (lane.intervalRole)];
        // Feedback zero permits exactly one successful reply, even if the initial
        // candidate was blocked. The cloud's beat/wall guards bound those retries.
        if (live.feedback == 0.0f)
            return lane.emitted == 0 ? live.level : 0.0;
        const double repeat = noteBeats[static_cast<std::size_t> (lane.settings.repeat)];
        return live.level * (elapsed == 0.0 ? 1.0
            : std::pow (static_cast<double> (live.feedback), elapsed / repeat));
    }
    return lane.firstGain * std::exp (-lane.decay * elapsed)
           / (1.0 + 0.08 * cloud.newerStrikes);
}

bool ParticleEngine::generate (Cloud& cloud, Lane& lane, Tick time, Tick blockStart,
                               MidiOutput& output, GeneratedNoteObserver* observer) noexcept
{
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
        return false;
    }
    SourceNote source;
    const int key = selectPitch (cloud, lane, source);
    if (key < 0)
    {
        increment (skippedCandidates_);
        return false;
    }
    const long double onsetBeat = onsetAt (cloud, beatAt (time));
    const double elapsed = static_cast<double> (std::max (0.0L, onsetBeat - lane.firstBeat));
    const double ceiling = decayUpper (cloud, lane, onsetBeat);
    // The first audible reply is deliberately attenuated, but not wave-modulated.
    // Subsequent local rises remain below an independently falling exponential cap.
    const auto& live = manualControls_[static_cast<std::size_t> (lane.intervalRole)];
    const double cycle = static_cast<double> (1 << live.cycle);
    const double wave = lane.emitted == 0 ? 1.0 : cloud.manual
        ? (1.0 - live.motion) + live.motion * (0.5 + 0.5 * std::cos (lane.phase
            + elapsed * 6.283185307179586 / cycle))
        : 0.725 + 0.275 * std::cos (lane.phase
            + elapsed * 6.283185307179586 / lane.wavePeriod);
    const double master = cloud.manual ? std::clamp (amount_ / 0.30, 0.0, 1.0 / 0.30) : 1.0;
    const double strength = ceiling * wave * master;
    // Manual master may raise the lane level, but never exceed its own source.
    const int maximum = cloud.manual ? std::clamp (source.velocity, 1, 127)
        : std::max (1, static_cast<int> (source.velocity * 0.48));
    const int velocity = std::clamp (static_cast<int> (std::lround (source.velocity * strength)),
                                     1, maximum);
    const double articulation = randomUnit (lane.randomState);
    const bool third = lane.intervalRole == IntervalRole::upThird
                       || lane.intervalRole == IntervalRole::downThird;
    const bool fifth = lane.intervalRole == IntervalRole::upFifth
                       || lane.intervalRole == IntervalRole::downFifth;
    const long double gate = cloud.manual
        ? static_cast<long double> (noteBeats[static_cast<std::size_t> (lane.settings.gate)])
        : third ? (articulation < 0.5 ? 0.75L : 1.0L)
        : fifth ? (articulation < 0.35 ? 0.5L : articulation < 0.80 ? 0.75L : 1.0L)
        : (articulation < 0.45 ? 1.0L : 1.5L);
    freeVoice->key = key;
    freeVoice->down = true;
    freeVoice->sostenuto = false;
    freeVoice->endBeat = onsetBeat + gate;
    freeVoice->earliestEnd = time;
    freeVoice->hardEnd = addTicks (time, durationSamples (cloud.manual ? 3.0 : 1.500));
    emitNote (static_cast<int> (time - blockStart), key, true, velocity, output);
    recordNoteOn (time);
    nextEmit_ = addTicks (time, durationSamples (0.020));
    if (observer != nullptr)
        observer->generatedNoteOn ({ static_cast<std::size_t> (lane.intervalRole), time,
                                     static_cast<int> (time - blockStart), velocity,
                                     static_cast<double> (beatAt (time) - cloud.triggerBeat),
                                     cloud.manual ? noteBeats[static_cast<std::size_t> (lane.settings.repeat)] : 0.0 });
    return true;
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
        rateHead_ = (rateHead_ + 1) % rateCapacity;
        --rateSize_;
    }
    return rateSize_ < rateCapacity;
}

void ParticleEngine::recordNoteOn (Tick time) noexcept
{
    recentOns_[static_cast<std::size_t> ((rateHead_ + rateSize_) % rateCapacity)] = time;
    ++rateSize_;
    increment (generatedNoteOns_);
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
            source.sounding = source.sostenuto = false;
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
        if (voice.down && (voiceTime (voice, time) == time))
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
    source.sounding = true;
    strike (key, velocity, time);
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
            if (cc == 120)
                source.sounding = source.sostenuto = false;
            else
                source.presses = 0;
        }
        for (auto& seed : clouds_)
            if (seed.activeLanes != 0 && seed.notes[0].key / 128 == channel)
                seed = {};
        refreshSources (channel);
        for (auto& voice : voices_)
        {
            if (voice.key < 0 || voice.key / 128 != channel)
                continue;
            releaseVoice (voice, time, blockStart, output);
            if (cc == 120)
                voice = {};
        }
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
    if (requestedMode_ == activeMode_)
        return;
    for (const auto& seed : clouds_)
        if (seed.activeLanes != 0)
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
                              MidiInput& input, MidiOutput& output, GeneratedNoteObserver* observer) noexcept
{
    if (numSamples <= 0)
        return;

    const Tick blockStart = sampleClock_;
    const Tick blockEnd = addTicks (blockStart, static_cast<Tick> (numSamples));
    const bool newEpoch = updateClock (transport, numSamples);
    const bool reset = controlsSeen_
                       && (controls.resetSerial != resetSerial_ || controls.seed != seed_);
    const int newRoot = std::clamp (controls.root, 0, 11);
    const Scale newScale = controls.scale == Scale::major ? Scale::major
                          : controls.scale == Scale::minor ? Scale::minor : Scale::unset;
    const bool harmonyChanged = controlsSeen_ && (root_ != newRoot || scale_ != newScale);
    const bool laneModeChanged = controlsSeen_ && manualLanes_ != controls.manualLanes;
    manualLanes_ = controls.manualLanes;
    updateManualControls (controls);
    root_ = newRoot;
    scale_ = newScale;
    amount_ = std::isfinite (controls.amount)
        ? std::clamp (static_cast<double> (controls.amount), 0.0, 1.0) : 0.0;
    requestedMode_ = controls.outputMode == OutputMode::particles ? OutputMode::particles
                                                                 : OutputMode::add;
    const bool newEnabled = amount_ > 0.0 && ! controls.bypass;
    if (newEpoch || reset || (enabled_ && ! newEnabled))
        cancelGeneration (blockStart, blockStart, output);
    else if (harmonyChanged || laneModeChanged)
        cancelClouds(); // Existing voices still own their original, finite Offs.
    seed_ = controls.seed;
    resetSerial_ = controls.resetSerial;
    if (! controlsSeen_ || newEpoch || reset)
        randomState_ = seed_ == 0 ? 0x6f7a6f31u : seed_;
    controlsSeen_ = true;
    enabled_ = newEnabled;
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
        runBefore (time, cursor, blockStart, output, observer);
        do
        {
            handleInput (event, time, blockStart, output);
            haveEvent = input.next (event);
        }
        while (haveEvent && event.sampleOffset == offset);
        runAt (time, blockStart, output, observer);
        cursor = time;
    }
    runBefore (blockEnd, cursor, blockStart, output, observer);
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
