// Standalone AUv2 MIDI test host. No JUCE headers, editor, audio device, or MIDI endpoints.
// Link: -framework AudioToolbox -framework AudioUnit -framework CoreFoundation -framework CoreMIDI
#include <AudioToolbox/AudioToolbox.h>
#include <AudioUnit/AudioUnit.h>
#include <CoreFoundation/CoreFoundation.h>
#include <CoreMIDI/CoreMIDI.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace
{
constexpr Float64 sampleRate = 48000.0;
constexpr UInt32 blockSize = 128;
constexpr std::int64_t samplesPerSecond = 48000;

void require (bool condition, const std::string& message)
{
    if (! condition)
        throw std::runtime_error (message);
}

void check (OSStatus status, const std::string& operation)
{
    if (status != noErr)
    {
        std::ostringstream text;
        text << operation << " failed: OSStatus " << status << " (0x"
             << std::hex << static_cast<UInt32> (status) << ')';
        throw std::runtime_error (text.str());
    }
}

template <typename T> struct CFRef
{
    explicit CFRef (T value = nullptr) : value (value) {}
    ~CFRef() { if (value != nullptr) CFRelease (value); }
    CFRef (const CFRef&) = delete;
    CFRef& operator= (const CFRef&) = delete;
    T value;
};

std::string utf8 (CFStringRef text)
{
    require (text != nullptr && CFGetTypeID (text) == CFStringGetTypeID(), "Expected a CFString");
    const auto capacity = CFStringGetMaximumSizeForEncoding (CFStringGetLength (text), kCFStringEncodingUTF8) + 1;
    std::vector<char> bytes (static_cast<std::size_t> (capacity));
    require (CFStringGetCString (text, bytes.data(), capacity, kCFStringEncodingUTF8), "Cannot decode CFString");
    return bytes.data();
}

CFTypeRef field (CFDictionaryRef dictionary, CFStringRef key, CFTypeID type)
{
    const auto value = CFDictionaryGetValue (dictionary, key);
    require (value != nullptr && CFGetTypeID (value) == type, "Invalid/missing Info.plist field: " + utf8 (key));
    return value;
}

UInt32 number (CFNumberRef value)
{
    std::int64_t result = 0;
    require (CFNumberGetValue (value, kCFNumberSInt64Type, &result)
             && result >= 0 && result <= UINT32_MAX, "Invalid component version/FourCC");
    return static_cast<UInt32> (result);
}

OSType fourCC (CFDictionaryRef dictionary, CFStringRef key)
{
    const auto value = CFDictionaryGetValue (dictionary, key);
    require (value != nullptr, "Missing component FourCC: " + utf8 (key));
    if (CFGetTypeID (value) == CFNumberGetTypeID())
        return number (static_cast<CFNumberRef> (value));
    const auto text = utf8 (static_cast<CFStringRef> (value));
    require (text.size() == 4, "Component FourCC must contain four ASCII bytes");
    OSType result = 0;
    for (const unsigned char byte : text)
    {
        require (byte < 128, "Non-ASCII component FourCC");
        result = (result << 8) | byte;
    }
    return result;
}

AudioComponent registerBundle (const char* path)
{
    CFRef<CFURLRef> url (CFURLCreateFromFileSystemRepresentation (
        nullptr, reinterpret_cast<const UInt8*> (path), static_cast<CFIndex> (std::strlen (path)), true));
    require (url.value != nullptr, "Cannot create component URL");
    CFRef<CFBundleRef> bundle (CFBundleCreate (nullptr, url.value));
    require (bundle.value != nullptr, "Cannot open component bundle: " + std::string (path));
    const auto info = CFBundleGetInfoDictionary (bundle.value);
    require (info != nullptr, "Bundle has no Info.plist");
    const auto components = static_cast<CFArrayRef> (field (info, CFSTR ("AudioComponents"), CFArrayGetTypeID()));
    require (CFArrayGetCount (components) > 0, "AudioComponents is empty");
    const auto first = CFArrayGetValueAtIndex (components, 0);
    require (CFGetTypeID (first) == CFDictionaryGetTypeID(), "AudioComponents[0] is not a dictionary");
    const auto entry = static_cast<CFDictionaryRef> (first);
    AudioComponentDescription description {};
    description.componentType = fourCC (entry, CFSTR ("type"));
    description.componentSubType = fourCC (entry, CFSTR ("subtype"));
    description.componentManufacturer = fourCC (entry, CFSTR ("manufacturer"));
    require (description.componentType == kAudioUnitType_MIDIProcessor
             && description.componentSubType == 0x4f6d6964u  // Omid
             && description.componentManufacturer == 0x4f7a6f41u, // OzoA
             "Expected AudioComponents[0] = aumi/Omid/OzoA");
    if (CFDictionaryGetValue (entry, CFSTR ("sandboxSafe")) == kCFBooleanTrue)
        description.componentFlags = kAudioComponentFlag_SandboxSafe;
    const auto name = static_cast<CFStringRef> (field (entry, CFSTR ("name"), CFStringGetTypeID()));
    const auto factoryName = static_cast<CFStringRef> (field (entry, CFSTR ("factoryFunction"), CFStringGetTypeID()));
    const auto version = number (static_cast<CFNumberRef> (field (entry, CFSTR ("version"), CFNumberGetTypeID())));
    CFRef<CFErrorRef> error;
    if (! CFBundleLoadExecutableAndReturnError (bundle.value, &error.value))
    {
        CFRef<CFStringRef> detail (error.value != nullptr ? CFErrorCopyDescription (error.value) : nullptr);
        throw std::runtime_error ("Cannot load AU executable (check architecture/signing): "
                                  + (detail.value != nullptr ? utf8 (detail.value) : std::string (path)));
    }
    const auto factory = reinterpret_cast<AudioComponentFactoryFunction> (
        CFBundleGetFunctionPointerForName (bundle.value, factoryName));
    require (factory != nullptr, "AU factory symbol not found: " + utf8 (factoryName));
    const auto component = AudioComponentRegister (&description, name, version, factory);
    require (component != nullptr, "AudioComponentRegister failed; no installed-component fallback is allowed");
    // AudioComponentRegister has no unregister API. Pin its factory's bundle until process exit;
    // unloading it after disposing an instance would leave a registered dangling function pointer.
    bundle.value = nullptr;
    std::cout << "Registered aumi/Omid/OzoA in this process only, factory " << utf8 (factoryName) << '\n';
    return component;
}

struct Event
{
    std::int64_t sample;
    MIDITimeStamp offset; // Raw packet timestamp: samples within this render, NEVER host ticks.
    std::array<Byte, 3> bytes;
    unsigned size = 3;
};

Event input (std::int64_t sample, Byte status, Byte data1, Byte data2)
{
    return { sample, static_cast<MIDITimeStamp> (sample % blockSize), { status, data1, data2 }, 3 };
}

std::string describe (const Event& event)
{
    std::ostringstream text;
    text << "sample=" << event.sample << " offset=" << event.offset << " bytes=";
    for (unsigned i = 0; i < event.size; ++i)
        text << std::hex << std::setw (2) << std::setfill ('0') << static_cast<unsigned> (event.bytes[i]) << ' ';
    return text.str();
}

struct Parameter
{
    AudioUnitParameterID id;
    std::string name;
    Float32 minimum, maximum;
    UInt32 flags;
};

class Host
{
public:
    explicit Host (AudioComponent component)
    {
        try
        {
            check (AudioComponentInstanceNew (component, &unit), "AudioComponentInstanceNew");
            require (unit != nullptr && AudioComponentInstanceGetComponent (unit) == component,
                     "AU instance does not belong to the process-local component");
            require (get<UInt32> (kAudioUnitProperty_ElementCount, kAudioUnitScope_Input) == 0
                     && get<UInt32> (kAudioUnitProperty_ElementCount, kAudioUnitScope_Output) == 1,
                     "Expected JUCE MIDIProcessor: no input bus and one dummy output bus");
            AudioStreamBasicDescription format {};
            format.mSampleRate = sampleRate;
            format.mFormatID = kAudioFormatLinearPCM;
            format.mFormatFlags = kAudioFormatFlagsNativeFloatPacked | kAudioFormatFlagIsNonInterleaved;
            format.mBytesPerPacket = format.mBytesPerFrame = sizeof (Float32);
            format.mFramesPerPacket = 1;
            format.mChannelsPerFrame = 2;
            format.mBitsPerChannel = 32;
            set (kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output, format);
            set (kAudioUnitProperty_MaximumFramesPerSlice, kAudioUnitScope_Global, blockSize);
            CFRef<CFArrayRef> outputs (get<CFArrayRef> (kAudioUnitProperty_MIDIOutputCallbackInfo));
            require (outputs.value != nullptr && CFGetTypeID (outputs.value) == CFArrayGetTypeID()
                     && CFArrayGetCount (outputs.value) == 1, "AU must advertise one MIDI output");
            captured.reserve (65536);
            AUMIDIOutputCallbackStruct midi { midiOutput, this };
            set (kAudioUnitProperty_MIDIOutputCallback, kAudioUnitScope_Global, midi);
            HostCallbackInfo clock {};
            clock.hostUserData = this;
            clock.beatAndTempoProc = beatAndTempo;
            clock.transportStateProc = transport;
            set (kAudioUnitProperty_HostCallbacks, kAudioUnitScope_Global, clock);
            enumerateParameters();
            check (AudioUnitInitialize (unit), "AudioUnitInitialize");
            initialized = true;
            const auto actual = get<AudioStreamBasicDescription> (kAudioUnitProperty_StreamFormat, kAudioUnitScope_Output);
            require (actual.mSampleRate == sampleRate && actual.mChannelsPerFrame == 2
                     && actual.mFormatID == format.mFormatID && actual.mFormatFlags == format.mFormatFlags
                     && actual.mBytesPerFrame == sizeof (Float32)
                     && get<UInt32> (kAudioUnitProperty_MaximumFramesPerSlice) == blockSize,
                     "AU did not retain 48000 Hz / 128 frames / Float32 stereo configuration");
        }
        catch (...)
        {
            dispose();
            throw;
        }
    }

    ~Host() { dispose(); }
    Host (const Host&) = delete;
    Host& operator= (const Host&) = delete;

    void setActual (const std::string& name, Float32 value, Float32 expectedMaximum)
    {
        const Parameter* match = nullptr;
        for (const auto& parameter : parameters)
        {
            auto lower = parameter.name;
            std::transform (lower.begin(), lower.end(), lower.begin(), [] (unsigned char c) { return static_cast<char> (std::tolower (c)); });
            if (lower.compare (0, name.size(), name) == 0
                && (lower.size() == name.size() || std::isspace (static_cast<unsigned char> (lower[name.size()]))))
            {
                require (match == nullptr, "Ambiguous AU parameter name: " + name);
                match = &parameter;
            }
        }
        require (match != nullptr, "Missing AU parameter: " + name);
        require (match->minimum == 0.0f && match->maximum == expectedMaximum,
                 "Unexpected native AU range for " + match->name);
        require ((match->flags & kAudioUnitParameterFlag_IsWritable) != 0
                 && (match->flags & kAudioUnitParameterFlag_IsReadable) != 0, "Parameter is not read/write: " + name);
        require (std::isfinite (value) && value >= match->minimum && value <= match->maximum, "Parameter value is outside native range");
        // AU values are native, not JUCE-normalized: Bloom is 1 in [0, 2], not 0.5.
        check (AudioUnitSetParameter (unit, match->id, kAudioUnitScope_Global, 0, value, 0), "Set " + match->name);
        Float32 readback = 0;
        check (AudioUnitGetParameter (unit, match->id, kAudioUnitScope_Global, 0, &readback), "Read " + match->name);
        require (std::isfinite (readback) && std::abs (readback - value) < 0.00001f, "Parameter readback mismatch: " + name);
    }

    std::int64_t position() const { return blockStart; }

    std::vector<Event> render (const std::vector<Event>& events, std::int64_t length)
    {
        require (length > 0 && length % blockSize == 0, "Render length must be a positive multiple of 128");
        for (std::size_t i = 0; i < events.size(); ++i)
            require (events[i].sample >= 0 && events[i].sample < length
                     && (i == 0 || events[i - 1].sample <= events[i].sample), "Invalid/unsorted input events");
        captured.clear();
        const auto previousCallbacks = callbackCount;
        std::size_t cursor = 0;
        std::array<Float32, blockSize> left {}, right {};
        // AudioBufferList is variable-sized; provide storage for TWO noninterleaved buffers.
        struct StereoList { UInt32 count; AudioBuffer buffers[2]; };
        static_assert (offsetof (StereoList, buffers) == offsetof (AudioBufferList, mBuffers));
        for (std::int64_t relative = 0; relative < length; relative += blockSize)
        {
            while (cursor < events.size() && events[cursor].sample < relative + blockSize)
            {
                const auto& event = events[cursor++];
                check (MusicDeviceMIDIEvent (unit, event.bytes[0], event.bytes[1], event.bytes[2],
                                            static_cast<UInt32> (event.sample - relative)), "MusicDeviceMIDIEvent");
            }
            left.fill (0);
            right.fill (0);
            StereoList audio { 2, { { 1, static_cast<UInt32> (sizeof (left)), left.data() },
                                    { 1, static_cast<UInt32> (sizeof (right)), right.data() } } };
            AudioTimeStamp timestamp {};
            timestamp.mSampleTime = static_cast<Float64> (blockStart);
            timestamp.mFlags = kAudioTimeStampSampleTimeValid;
            AudioUnitRenderActionFlags flags = 0;
            rendering = true;
            const auto result = AudioUnitRender (unit, &flags, &timestamp, 0, blockSize,
                                                 reinterpret_cast<AudioBufferList*> (&audio));
            rendering = false;
            require (captureError == nullptr, "MIDI capture failed at block " + std::to_string (blockStart)
                                             + ": " + (captureError != nullptr ? captureError : ""));
            check (result, "AudioUnitRender at sample " + std::to_string (blockStart));
            blockStart += blockSize;
        }
        require (cursor == events.size(), "Not all input MIDI events were submitted");
        require (events.empty() || callbackCount > previousCallbacks,
                 "MIDIOutputCallback was never invoked; MIDI output capture is not working");
        return captured;
    }

    void reset() // Call only after verifying BOTH input and output keys and CC64 are clear.
    {
        check (AudioUnitReset (unit, kAudioUnitScope_Global, 0), "AudioUnitReset");
        // Keep time monotonic across tests; Reset must not masquerade as a transport jump.
    }

    void close()
    {
        check (AudioUnitUninitialize (unit), "AudioUnitUninitialize");
        initialized = false;
        check (AudioComponentInstanceDispose (unit), "AudioComponentInstanceDispose");
        unit = nullptr;
    }

private:
    template <typename T> T get (AudioUnitPropertyID property, AudioUnitScope scope = kAudioUnitScope_Global,
                               AudioUnitElement element = 0)
    {
        T result {};
        UInt32 size = sizeof (result);
        check (AudioUnitGetProperty (unit, property, scope, element, &result, &size), "Get AU property " + std::to_string (property));
        require (size == sizeof (result), "Unexpected AU property size");
        return result;
    }

    template <typename T> void set (AudioUnitPropertyID property, AudioUnitScope scope, const T& value)
    {
        check (AudioUnitSetProperty (unit, property, scope, 0, &value, sizeof (value)), "Set AU property " + std::to_string (property));
    }

    void enumerateParameters()
    {
        UInt32 size = 0;
        Boolean writable = false;
        check (AudioUnitGetPropertyInfo (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, &size, &writable), "Get parameter list size");
        require (size > 0 && size % sizeof (AudioUnitParameterID) == 0, "Invalid/empty AU parameter list");
        std::vector<AudioUnitParameterID> ids (size / sizeof (AudioUnitParameterID));
        const auto expectedSize = size;
        check (AudioUnitGetProperty (unit, kAudioUnitProperty_ParameterList, kAudioUnitScope_Global, 0, ids.data(), &size), "Get parameter list");
        require (size == expectedSize, "AU parameter list changed while reading");
        for (const auto id : ids)
        {
            const auto info = get<AudioUnitParameterInfo> (kAudioUnitProperty_ParameterInfo, kAudioUnitScope_Global, id);
            const bool hasName = (info.flags & kAudioUnitParameterFlag_HasCFNameString) != 0;
            const bool release = (info.flags & kAudioUnitParameterFlag_CFNameRelease) != 0;
            CFRef<CFStringRef> ownedName (hasName && release ? info.cfNameString : nullptr);
            CFRef<CFStringRef> ownedUnit (release && info.unit == kAudioUnitParameterUnit_CustomUnit ? info.unitName : nullptr);
            const auto name = hasName ? utf8 (info.cfNameString)
                                      : std::string (info.name, std::find (info.name, info.name + sizeof (info.name), '\0'));
            parameters.push_back ({ id, name, info.minValue, info.maxValue, info.flags });
            std::cout << "AU parameter " << name << " id=" << id << " native=[" << info.minValue << ',' << info.maxValue << "]\n";
        }
    }

    OSStatus captureFailure (const char* reason) noexcept
    {
        if (captureError == nullptr)
            captureError = reason;
        return kAudioUnitErr_InvalidPropertyValue;
    }

    static OSStatus midiOutput (void* context, const AudioTimeStamp* timestamp, UInt32 output,
                                const MIDIPacketList* packets) noexcept
    {
        auto& self = *static_cast<Host*> (context);
        ++self.callbackCount;
        if (! self.rendering || timestamp == nullptr || packets == nullptr || output != 0)
            return self.captureFailure ("Callback outside render, null payload, or unexpected MIDI output bus");
        if ((timestamp->mFlags & kAudioTimeStampSampleTimeValid) == 0
            || timestamp->mSampleTime != static_cast<Float64> (self.blockStart))
            return self.captureFailure ("Callback AudioTimeStamp does not match the current render block");
        // Callback errors must be sticky: JUCE's pushMidiOutput ignores its returned OSStatus.
        // Never throw through this C ABI callback; storage is reserved before initialization.
        try
        {
            const MIDIPacket* packet = packets->numPackets != 0 ? &packets->packet[0] : nullptr;
            for (UInt32 i = 0; i < packets->numPackets; ++i, packet = MIDIPacketNext (packet))
            {
                if (packet->timeStamp >= blockSize)
                    return self.captureFailure ("MIDIPacket.timeStamp is outside [0, 128); expected sample offset, not host ticks");
                if (packet->length == 0)
                    return self.captureFailure ("Empty MIDI packet");
                // CoreMIDI can coalesce simultaneous messages into ONE packet. Decode all of
                // them without discarding the shared offset. Running status is forbidden here.
                for (std::size_t byte = 0; byte < packet->length;)
                {
                    const auto status = packet->data[byte];
                    if (status < 0x80 || status >= 0xf0)
                        return self.captureFailure ("Unexpected non-channel MIDI/status byte (this test sends no SysEx/system messages)");
                    const unsigned count = (status & 0xf0) == 0xc0 || (status & 0xf0) == 0xd0 ? 2u : 3u;
                    if (byte + count > packet->length)
                        return self.captureFailure ("Truncated MIDI message in packet");
                    Event event { self.blockStart + static_cast<std::int64_t> (packet->timeStamp), packet->timeStamp, {}, count };
                    for (unsigned j = 0; j < count; ++j)
                    {
                        event.bytes[j] = packet->data[byte + j];
                        if (j != 0 && event.bytes[j] >= 0x80)
                            return self.captureFailure ("Invalid MIDI data byte");
                    }
                    if (! self.captured.empty() && event.sample < self.captured.back().sample)
                        return self.captureFailure ("MIDI output is not ordered by sample offset");
                    if (self.captured.size() == self.captured.capacity())
                        return self.captureFailure ("MIDI capture capacity exceeded");
                    self.captured.push_back (event);
                    byte += count;
                }
            }
        }
        catch (...)
        {
            return self.captureFailure ("Exception while copying MIDI callback data");
        }
        return noErr;
    }

    static OSStatus beatAndTempo (void* context, Float64* beat, Float64* tempo) noexcept
    {
        const auto& self = *static_cast<Host*> (context);
        if (beat != nullptr) *beat = static_cast<Float64> (self.blockStart) / (sampleRate * 0.5);
        if (tempo != nullptr) *tempo = 120.0;
        return noErr;
    }

    static OSStatus transport (void* context, Boolean* playing, Boolean* changed, Float64* sample,
                               Boolean* cycling, Float64* cycleStart, Float64* cycleEnd) noexcept
    {
        const auto& self = *static_cast<Host*> (context);
        if (playing != nullptr) *playing = true;
        if (changed != nullptr) *changed = false;
        if (sample != nullptr) *sample = static_cast<Float64> (self.blockStart);
        if (cycling != nullptr) *cycling = false;
        if (cycleStart != nullptr) *cycleStart = 0;
        if (cycleEnd != nullptr) *cycleEnd = 0;
        return noErr;
    }

    void dispose() noexcept
    {
        if (unit != nullptr)
        {
            if (initialized) (void) AudioUnitUninitialize (unit);
            (void) AudioComponentInstanceDispose (unit);
            unit = nullptr;
        }
    }

    AudioUnit unit = nullptr;
    bool initialized = false, rendering = false;
    std::int64_t blockStart = 0;
    std::uint64_t callbackCount = 0;
    const char* captureError = nullptr;
    std::vector<Event> captured;
    std::vector<Parameter> parameters;
};

bool isOn (const Event& event) { return (event.bytes[0] & 0xf0) == 0x90 && event.bytes[2] != 0; }

std::size_t checkBalancedAndIdle (const std::vector<Event>& events)
{
    std::array<int, 16 * 128> keys {};
    std::array<bool, 16> sustain {};
    std::size_t ons = 0;
    for (const auto& event : events)
    {
        const auto type = event.bytes[0] & 0xf0;
        const auto channel = event.bytes[0] & 0x0f;
        auto& count = keys[static_cast<std::size_t> (channel * 128 + event.bytes[1])];
        if (isOn (event))
        {
            require (count == 0, "Same-key overlap: " + describe (event));
            ++count;
            ++ons;
        }
        else if (type == 0x80 || type == 0x90)
        {
            require (count > 0, "Unowned Note Off: " + describe (event));
            --count;
        }
        else if (type == 0xb0 && event.bytes[1] == 64)
            sustain[static_cast<std::size_t> (channel)] = event.bytes[2] >= 64;
    }
    require (std::all_of (keys.begin(), keys.end(), [] (int count) { return count == 0; }), "Keys still down at end of render");
    require (std::none_of (sustain.begin(), sustain.end(), [] (bool down) { return down; }), "CC64 sustain is still down");
    return ons;
}
} // namespace

int main (int argc, char** argv)
{
    if (argc != 2)
    {
        std::cerr << "Usage: ozoGlintAUHostTests /absolute/path/ozo GLINT.component\n";
        return 1;
    }
    try
    {
        Host host (registerBundle (argv[1]));
        host.setActual ("amount", 0.0f, 1.0f);
        host.setActual ("character", 1.0f, 2.0f); // Bloom, from the published native choice order.
        host.setActual ("output", 0.0f, 1.0f); // Add
        const std::vector<Event> dryInput {
            input (17, 0x90, 60, 83),
            input (93, 0x80, 60, 47),
            input (128 + 29, 0xb0, 64, 127),
            input (256 + 101, 0xb0, 64, 0)
        };
        const auto dryOutput = host.render (dryInput, 512);
        require (dryOutput.size() == dryInput.size(), "Amount=0/Add changed MIDI event count: expected "
                 + std::to_string (dryInput.size()) + ", got " + std::to_string (dryOutput.size()));
        for (std::size_t i = 0; i < dryInput.size(); ++i)
        {
            const auto& expected = dryInput[i];
            const auto& actual = dryOutput[i];
            require (actual.sample == expected.sample && actual.offset == expected.offset
                     && actual.size == expected.size && actual.bytes == expected.bytes,
                     "Pass-through mismatch: expected " + describe (expected) + "; got " + describe (actual));
        }
        checkBalancedAndIdle (dryInput);
        checkBalancedAndIdle (dryOutput);
        std::cout << "PASS AU Amount=0/Add: Note On offset 17, Note Off offset 93, separate-block CC64 exact bytes/offsets\n";

        // The same instance is reused only after all source/output notes AND sustain are clear.
        host.reset();
        host.setActual ("amount", 0.8f, 1.0f);
        host.setActual ("character", 1.0f, 2.0f); // Bloom
        host.setActual ("output", 1.0f, 1.0f); // Particles
        require (host.render ({}, blockSize).empty(), "Reset/idle mode switch emitted unexpected MIDI");
        const auto origin = host.position();
        const std::vector<Event> chord {
            input (23, 0x90, 60, 90), // C4, E4, G4 share a packet timestamp when coalesced.
            input (23, 0x90, 64, 80),
            input (23, 0x90, 67, 75),
            input (samplesPerSecond + 93, 0x80, 60, 0),
            input (samplesPerSecond + 93, 0x80, 64, 0),
            input (samplesPerSecond + 93, 0x80, 67, 0)
        };
        const auto particles = host.render (chord, 10 * samplesPerSecond);
        checkBalancedAndIdle (chord);
        const auto generatedOns = checkBalancedAndIdle (particles);
        require (generatedOns > 3, "Bloom did not produce additional notes beyond the three source notes");
        for (const auto& event : particles)
            if (isOn (event))
                require (event.sample > origin + 23, "Particles leaked an original Note On: " + describe (event));
        std::cout << "PASS AU Amount=0.8/Particles/Bloom: " << generatedOns
                  << " generated Note Ons, balanced offs, no original On leakage, no keys/pedal down at 10 seconds\n";
        host.close();
        std::cout << "PASS AU tests (process-local registration; no editor, device, installation, or DAW)\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << "FAIL AU " << error.what() << '\n';
        return 1;
    }
    catch (...)
    {
        std::cerr << "FAIL AU unknown exception\n";
        return 1;
    }
}
