// Tests.cpp — LATTICE drum machine test suite.
//
// Builds as the JUCE console target `LatticeTests` (CMake already links
// juce_audio_utils and defines JUCE_PLUGINHOST_VST3=1).
//
// Coverage
// --------
//  * Model (header-only, always compiled): 8 tracks x 16 bars contiguous,
//    seed determinism, density response, ghost-off (no 40), locked-track
//    preservation across 16 bars, 7/8 & 5/4 meters, invalid-group rejection,
//    3/6 subdivision on the 60-tick (32nd-note) grid with no drift, GM note
//    mapping (Crash = GM 49).  NEW: 100 BPM default, Crash lane, b0/b2 motif
//    inheritance (identical except Crash), development/fill/space == 0 long
//    phrase inheritance, barLocked (all 16) & track lock (all 8) preservation,
//    space reserves trailing rest.
//  * MIDI export (guarded by __has_include("Processor.h")): instantiate
//    LatticeProcessor, exportMidi(), then verify the juce::MidiFile:
//    480 PPQ, 8 GM pitches (incl. Crash 49), channel 10, tempo/time-sig,
//    16-bar (30720-tick) end, and 60-tick (32nd) resolution.
//  * State (guarded by Processor.h): v2 save/restore of the full 16x8 pattern
//    plus development/fill/space/gridStep/barLocked, and v1 migration
//    (4 bars / 7 tracks -> gridStep 120).
//  * Real VST3 (needs a .vst3 path, no Processor.h required): load with
//    VST3PluginFormat, createPluginInstance, prepareToPlay(48000/256),
//    send each of the 8 GM notes on, processBlock -> finite / peak / nonzero,
//    then state roundtrip.
//  * GUI (--gui + .vst3 path): open the real editor in a DocumentWindow,
//    run the MessageManager dispatch loop, snapshot after 1500ms to
//    outputs/plugin-ui.png via createComponentSnapshot + PNGImageFormat.
//
// Usage
// -----
//   LatticeTests                                  # model + MIDI + state tests
//   LatticeTests /path/to/ozoLATTICE.vst3          # + plugin load/process/state
//   LatticeTests --gui /path/to/ozoLATTICE.vst3    # + screenshot of real editor
//
// Exit code is non-zero if any CHECK fails; final line prints PASS/FAIL/PEAK.

#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <cstring>
#include <set>
#include <string>
#include <vector>
#include <array>
#include <functional>

#include "Model.h"

#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>   // pulls in copyXmlToBinary / getXmlFromBinary
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_gui_extra/juce_gui_extra.h>

// ---------------------------------------------------------------------------
// Tiny assertion harness. Real failures flip the process exit code to non-zero.
// ---------------------------------------------------------------------------
static int  g_pass = 0;
static int  g_fail = 0;
static float g_peak = 0.0f;
static bool g_usedProcessor = false;

#define CHECK(cond, msg)                                                 \
    do {                                                                 \
        if (!!(cond)) { ++g_pass; std::printf("  ok    %s\n", msg); }    \
        else           { ++g_fail; std::fprintf(stderr, "  FAIL  %s\n", msg); } \
    } while (0)

static void recordPeak(float p) { if (p > g_peak) g_peak = p; }

static void section(const char* name) { std::printf("\n--- %s ---\n", name); }

// GM drum note numbers, in lane order (matches lattice::notes, 8 lanes incl. Crash).
static const int kGM[8] = { 36, 38, 37, 42, 46, 45, 51, 49 };

// ===========================================================================
// MODEL TESTS
// ===========================================================================
static int countHits(const lattice::Pattern& p)
{
    int n = 0;
    for (int b = 0; b < lattice::phraseBars; ++b)
        for (int r = 0; r < lattice::tracks; ++r)
            for (const auto& c : p.bars[b][r])
                if (c.velocity > 0) ++n;
    return n;
}

static void runModelTests()
{
    section("Model: 8 tracks / 16 bars contiguous");
    {
        lattice::Pattern p;
        CHECK(p.bars.size() == lattice::phraseBars, "16 bars present");
        const int ticks = p.barTicks();
        CHECK(ticks == 1920, "4/4 barTicks == 1920");
        for (int b = 0; b < lattice::phraseBars; ++b)
        {
            for (int r = 0; r < lattice::tracks; ++r)
            {
                const auto& lane = p.bars[b][r];
                CHECK(!lane.empty(), "lane non-empty");
                int pos = 0;
                bool contiguous = true;
                for (const auto& c : lane)
                {
                    if (c.start != pos) contiguous = false;
                    pos += c.length;
                }
                CHECK(contiguous, "lane cells are contiguous in time");
                CHECK(pos == ticks, "lane covers the full bar [0,barTicks)");
            }
        }
    }

    section("Model: seed determinism");
    {
        lattice::Pattern a, b;                 // both default to seed 1729
        CHECK(a.seed == b.seed, "default seeds equal");
        bool identical = (a.bars.size() == b.bars.size());
        for (int bb = 0; bb < lattice::phraseBars && identical; ++bb)
            for (int r = 0; r < lattice::tracks && identical; ++r)
            {
                const auto& la = a.bars[bb][r];
                const auto& lb = b.bars[bb][r];
                if (la.size() != lb.size()) { identical = false; break; }
                for (size_t i = 0; i < la.size(); ++i)
                    if (la[i].start != lb[i].start || la[i].length != lb[i].length
                        || la[i].velocity != lb[i].velocity) { identical = false; break; }
            }
        CHECK(identical, "same seed -> byte-identical pattern");

        // Different seed should (with overwhelming probability) differ.
        lattice::Pattern c, d;
        c.seed = 42;  c.generate();
        d.seed = 43;  d.generate();
        bool differs = (countHits(c) != countHits(d));
        if (!differs)
        {
            bool diff2 = false;
            for (int bb = 0; bb < lattice::phraseBars && !diff2; ++bb)
                for (int r = 0; r < lattice::tracks && !diff2; ++r)
                {
                    const auto& lc = c.bars[bb][r];
                    const auto& ld = d.bars[bb][r];
                    if (lc.size() != ld.size()) { diff2 = true; break; }
                    for (size_t i = 0; i < lc.size(); ++i)
                        if (lc[i].velocity != ld[i].velocity) { diff2 = true; break; }
                }
            differs = diff2;
        }
        CHECK(differs, "different seed -> different pattern");
    }

    section("Model: density response");
    {
        lattice::Pattern lo, hi;
        lo.density = 0.15f; lo.generate();
        hi.density = 0.95f; hi.generate();
        int loHits = countHits(lo), hiHits = countHits(hi);
        CHECK(hiHits > loHits, "higher density yields more hits");
        std::printf("        (low=%.2f hits=%d, high=%.2f hits=%d)\n",
                    (double)lo.density, loHits, (double)hi.density, hiHits);
    }

    section("Model: ghost notes disabled -> no velocity 40");
    {
        lattice::Pattern p;
        p.ghost = false;
        p.generate();
        bool any40 = false;
        for (int b = 0; b < lattice::phraseBars && !any40; ++b)
            for (int r = 0; r < lattice::tracks && !any40; ++r)
                for (const auto& c : p.bars[b][r])
                    if (c.velocity == 40) { any40 = true; break; }
        CHECK(!any40, "no ghost (velocity 40) notes when ghost disabled");
    }

    section("Model: locked track fully preserved across 16 bars");
    {
        lattice::Pattern p;
        p.generate();                          // initial 4/4 pattern
        std::array<std::vector<lattice::Cell>, lattice::phraseBars> before;
        for (int b = 0; b < lattice::phraseBars; ++b) before[b] = p.bars[b][2];  // snapshot track 2

        p.locked[2] = true;
        p.seed = 555;                          // change seed so unlocked lanes change
        p.generate();

        bool same = true;
        for (int b = 0; b < lattice::phraseBars; ++b)
        {
            const auto& after = p.bars[b][2];
            if (after.size() != before[b].size()) { same = false; break; }
            for (size_t i = 0; i < after.size(); ++i)
                if (after[i].start != before[b][i].start
                    || after[i].length != before[b][i].length
                    || after[i].velocity != before[b][i].velocity) { same = false; break; }
        }
        CHECK(same, "locked track unchanged after regenerate (all 16 bars)");
    }

    section("Model: 7/8 and 5/4 meters");
    {
        auto coverageOk = [](const lattice::Pattern& p, int ticks) -> bool
        {
            for (int b = 0; b < lattice::phraseBars; ++b)
                for (int r = 0; r < lattice::tracks; ++r)
                {
                    const auto& lane = p.bars[b][r];
                    int pos = 0;
                    for (const auto& c : lane) { if (c.start != pos) return false; pos += c.length; }
                    if (pos != ticks) return false;
                }
            return true;
        };

        lattice::Pattern seven8;
        seven8.meter(7, 8);
        CHECK(seven8.numerator == 7 && seven8.denominator == 8, "7/8 meter set");
        CHECK(seven8.valid(), "7/8 is valid");
        CHECK(seven8.barTicks() == 7 * 1920 / 8, "7/8 barTicks == 1680");
        seven8.generate();
        CHECK(coverageOk(seven8, 1680), "7/8 lanes contiguous & full-bar (16 bars)");

        lattice::Pattern five4;
        five4.meter(5, 4);
        CHECK(five4.numerator == 5 && five4.denominator == 4, "5/4 meter set");
        CHECK(five4.valid(), "5/4 is valid");
        CHECK(five4.barTicks() == 5 * 1920 / 4, "5/4 barTicks == 2400");
        five4.generate();
        CHECK(coverageOk(five4, 2400), "5/4 lanes contiguous & full-bar (16 bars)");
    }

    section("Model: invalid grouping rejected");
    {
        lattice::Pattern p;                    // default 4/4, groups {8}
        p.groups = { 1,1,1,1,1,1,1,1 };        // sum 8 -> valid
        CHECK(p.valid(), "eighth-note grouping of eight 1s is valid");
        p.groups = { 5, 2 };                   // sum 7 != 8 -> invalid
        CHECK(!p.valid(), "grouping total != eighths is invalid");
        CHECK(!p.generate(), "generate() rejects invalid grouping");
        p.groups = {};                         // empty
        CHECK(!p.valid(), "empty grouping is invalid");
        p.groups = { 0, 8 };                   // contains a zero
        CHECK(!p.valid(), "grouping containing 0 is invalid");
        p.groups = { 8 };
        p.numerator = 0;                       // out of range
        CHECK(!p.valid(), "numerator 0 is invalid");
    }

    section("Model: 3/6 subdivision on 60-tick grid, equal duration, no drift");
    {
        // /3 on a 4-cell (240-tick) quarter-note region -> three 80-tick cells.
        lattice::Pattern p3; p3.generate();
        int a = 0, z = 3;
        int begin = p3.bars[0][3][a].start;                          // 0
        int end   = p3.bars[0][3][z].start + p3.bars[0][3][z].length; // 240
        CHECK(p3.subdivide(0, 3, a, z, 3), "subdivide /3 succeeds");
        {
            const auto& l = p3.bars[0][3];
            CHECK(l[a].start == begin && (l[a + 2].start + l[a + 2].length) == end,
                  "/3 preserves region start & end (same duration)");
            CHECK(l[a].length == 80 && l[a + 1].length == 80 && l[a + 2].length == 80,
                  "/3 produces three equal 80-tick cells (triplet)");
            CHECK(l[a + 3].start == 240, "/3 next slot stays on the 60-tick grid (no drift)");
        }
        // Re-subdivide one of the new 80 cells by 2 -> proves grid stays aligned.
        CHECK(p3.subdivide(0, 3, a, a, 2), "re-subdivide a /3 cell by /2 succeeds");
        CHECK(p3.bars[0][3][a].length == 40 && p3.bars[0][3][a + 1].length == 40,
              "no drift: re-subdivision lands on exact 40-tick grid");

        // /6 on a 12-cell (720-tick) region -> six 120-tick cells.
        lattice::Pattern p6; p6.generate();
        int a6 = 0, z6 = 11;
        int b6 = p6.bars[0][3][a6].start;
        int e6 = p6.bars[0][3][z6].start + p6.bars[0][3][z6].length;
        CHECK(p6.subdivide(0, 3, a6, z6, 6), "subdivide /6 succeeds");
        {
            const auto& l = p6.bars[0][3];
            bool aligned = true; int pos = 0;
            for (const auto& c : l) { if (c.start != pos) aligned = false; pos += c.length; }
            CHECK(l[a6].start == b6 && (l[a6 + 5].start + l[a6 + 5].length) == e6,
                  "/6 preserves region duration");
            for (int i = 0; i < 6; ++i) if (l[a6 + i].length != 120) aligned = false;
            CHECK(aligned, "/6 cells equal 120 and contiguous (no drift)");
            CHECK(pos == p6.barTicks(), "/6 keeps full-bar coverage");
        }
        // Restore the /6 region to the 120-tick baseline grid (count=0 snaps to 120).
        CHECK(p6.subdivide(0, 3, a6, z6, 0), "restore /6 region to 120-tick baseline succeeds");
        CHECK(p6.bars[0][3][a6].length == 120 && p6.bars[0][3][a6 + 5].length == 120,
              "restored region on exact 120 grid (no drift)");

        // Invalid region must be rejected (no crash, no partial edit).
        lattice::Pattern pinv; pinv.generate();
        CHECK(!pinv.subdivide(0, 3, 5, 2, 3), "subdivide rejected when a > z");
        CHECK(!pinv.subdivide(0, 3, 0, 999, 3), "subdivide rejected when z out of range");
    }

    section("Model: true off-grid tuplets (60-tick slots)");
    {
        lattice::Pattern p; auto untouched = p.bars[0][2];
        CHECK(p.subdivide(0,3,0,3,3), "quarter-note region -> triplet");
        CHECK(p.bars[0][3][1].start == 80 && p.bars[0][3][2].start == 160 && p.bars[0][3][3].start == 240,
              "triplets at 0/80/160; next slot remains 240 (60-grid)");
        CHECK(p.subdivide(0,3,0,2,6), "same quarter-note region -> sextuplet");
        CHECK(p.bars[0][3][1].start == 40 && p.bars[0][3][5].start == 200 && p.bars[0][3][6].start == 240,
              "sextuplets at 40-tick intervals; no drift");
        CHECK(p.bars[0][2].size() == untouched.size() && p.bars[0][2][1].start == 60, "other lane unchanged");
        CHECK(p.subdivide(0,3,0,5,0) && p.bars[0][3][0].length == 120 && p.bars[0][3][1].length == 120,
              "restore sextuplet region to 120-tick baseline");
    }

    section("Model: GM note mapping (Crash = GM 49)");
    {
        bool gm = true;
        std::set<int> uniq;
        for (int r = 0; r < 8; ++r)
        {
            if (lattice::notes[r] != kGM[r]) gm = false;
            if (lattice::notes[r] < 35 || lattice::notes[r] > 81) gm = false;  // GM percussion range
            uniq.insert(lattice::notes[r]);
        }
        CHECK(gm, "lanes map to GM drum notes {36,38,37,42,46,45,51,49}");
        CHECK(uniq.size() == 8, "GM notes are unique across 8 lanes");
        (void)lattice::names;  // silence unused warning for the name table
    }

    // ----- NEW generative / interface tests -----------------------------------

    section("Model: default tempo is 100 BPM");
    {
        lattice::Pattern p;
        CHECK(p.bpm == 100.0, "default bpm == 100");
    }

    section("Model: Crash lane is GM 49 (track 7)");
    {
        CHECK(lattice::notes[7] == 49, "Crash note == GM 49");
        CHECK(std::string(lattice::names[7]) == "Crash", "track 7 is named Crash");
    }

    section("Model: motif bars 0 and 2 identical except Crash");
    {
        lattice::Pattern p; p.generate();
        const auto& b0 = p.bars[0];
        const auto& b2 = p.bars[2];
        bool matchExceptCrash = true;
        for (int r = 0; r < lattice::tracks; ++r)
            for (size_t s = 0; s < b0[r].size(); ++s)
            {
                int v0 = b0[r][s].velocity, v2 = b2[r][s].velocity;
                if (v0 != v2)
                {
                    // Bar 0's Crash punctuation also clears hat/ride at slot 0;
                    // those are part of the Crash articulation, so allow them.
                    if (!(r == 7 || ((r == 3 || r == 6) && s == 0))) matchExceptCrash = false;
                }
            }
        CHECK(matchExceptCrash, "bars 0 and 2 share the motif; only Crash (and its slot-0 hat/ride clear) differ");
        CHECK(b0[7][0].velocity == 127 && b2[7][0].velocity == 0, "Crash present in bar0, absent in bar2");
    }

    section("Model: dev=0 fill=0 space=0 -> long-phrase motif inheritance");
    {
        lattice::Pattern p; p.development = 0; p.fill = 0; p.space = 0; p.generate();
        auto eq = [&](int x, int y)
        {
            for (int r = 0; r < lattice::tracks; ++r)
            {
                if (p.bars[x][r].size() != p.bars[y][r].size()) return false;
                for (size_t s = 0; s < p.bars[x][r].size(); ++s)
                    if (p.bars[x][r][s].velocity != p.bars[y][r][s].velocity) return false;
            }
            return true;
        };
        auto sameExceptCrash = [&](int x, int y)
        {
            for (int r = 0; r < lattice::tracks; ++r)
                for (size_t s = 0; s < p.bars[x][r].size(); ++s)
                {
                    int vx = p.bars[x][r][s].velocity, vy = p.bars[y][r][s].velocity;
                    if (vx != vy && !(r == 7 || ((r == 3 || r == 6) && s == 0))) return false;
                }
            return true;
        };
        CHECK(eq(2,4) && eq(4,6) && eq(2,10) && eq(2,14), "motif[0] bars (2,4,6,10,14) identical");
        CHECK(eq(1,3) && eq(3,5) && eq(1,9), "motif[1] bars (1,3,5,9) identical");
        CHECK(sameExceptCrash(0,2), "bar0 inherits motif[0] (differs from bar2 only by Crash)");
        CHECK(sameExceptCrash(8,10), "bar8 inherits motif[0] (differs from bar10 only by Crash)");
    }

    section("Model: barLocked (all 16) preserves every bar");
    {
        lattice::Pattern p; p.generate();
        std::array<std::array<std::vector<lattice::Cell>, lattice::tracks>, lattice::phraseBars> before;
        for (int b = 0; b < lattice::phraseBars; ++b)
            for (int r = 0; r < lattice::tracks; ++r) before[b][r] = p.bars[b][r];
        for (int b = 0; b < lattice::phraseBars; ++b) p.barLocked[b] = true;
        p.seed = 98765; p.generate();
        bool same = true;
        for (int b = 0; b < lattice::phraseBars && same; ++b)
            for (int r = 0; r < lattice::tracks && same; ++r)
            {
                const auto& after = p.bars[b][r];
                if (after.size() != before[b][r].size()) { same = false; break; }
                for (size_t s = 0; s < after.size(); ++s)
                    if (after[s].start != before[b][r][s].start
                        || after[s].length != before[b][r][s].length
                        || after[s].velocity != before[b][r][s].velocity) { same = false; break; }
            }
        CHECK(same, "all 16 bars unchanged when fully barLocked");
        // Sanity: without the lock the new seed would have changed the pattern.
        // (First-cell velocities are motif-deterministic, so compare every cell.)
        lattice::Pattern q; q.seed = 98765; q.generate();
        bool differs = false;
        for (int b = 0; b < lattice::phraseBars && !differs; ++b)
            for (int r = 0; r < lattice::tracks && !differs; ++r)
            {
                if (q.bars[b][r].size() != before[b][r].size()) { differs = true; break; }
                for (size_t s = 0; s < q.bars[b][r].size(); ++s)
                    if (q.bars[b][r][s].velocity != before[b][r][s].velocity) { differs = true; break; }
            }
        CHECK(differs, "sanity: unlocked regenerate with new seed differs (lock is what preserved)");
    }

    section("Model: locked tracks (all 8) preserved across 16 bars");
    {
        lattice::Pattern p; p.generate();
        std::array<std::array<std::vector<lattice::Cell>, lattice::tracks>, lattice::phraseBars> before;
        for (int b = 0; b < lattice::phraseBars; ++b)
            for (int r = 0; r < lattice::tracks; ++r) before[b][r] = p.bars[b][r];
        for (int r = 0; r < lattice::tracks; ++r) p.locked[r] = true;
        p.seed = 13579; p.generate();
        bool same = true;
        for (int b = 0; b < lattice::phraseBars && same; ++b)
            for (int r = 0; r < lattice::tracks && same; ++r)
            {
                const auto& after = p.bars[b][r];
                if (after.size() != before[b][r].size()) { same = false; break; }
                for (size_t s = 0; s < after.size(); ++s)
                    if (after[s].start != before[b][r][s].start
                        || after[s].length != before[b][r][s].length
                        || after[s].velocity != before[b][r][s].velocity) { same = false; break; }
            }
        CHECK(same, "all 8 tracks unchanged across 16 bars when fully locked");
    }

    section("Model: space protects trailing whitespace");
    {
        auto trailingSilent = [](lattice::Pattern& pp, int bar, int fromSlot)
        {
            int steps = pp.barTicks() / 60;
            if (fromSlot < 0 || fromSlot >= steps) return false;
            for (int r = 0; r < lattice::tracks; ++r)
                for (int s = fromSlot; s < steps; ++s)
                    if (pp.bars[bar][r][s].velocity != 0) return false;
            return true;
        };
        lattice::Pattern lo; lo.development = 0; lo.fill = 0; lo.space = 0.0; lo.generate();
        lattice::Pattern hi; hi.development = 0; hi.fill = 0; hi.space = 0.5; hi.generate();
        int steps = lo.barTicks() / 60;                 // 32
        int restHi = std::min(steps - 1, int(0.5 * 6) * 2); // 12
        CHECK(!trailingSilent(lo, 1, steps - restHi), "space=0 leaves content in odd-bar trailing region");
        CHECK(trailingSilent(hi, 1, steps - restHi), "space=0.5 protects trailing whitespace (odd bar silent)");
        // Boundary odd bar (b%4==3) gets a larger rest at space=1.0.
        lattice::Pattern hb; hb.development = 0; hb.fill = 0; hb.space = 1.0; hb.generate();
        int restBound = std::min(steps - 1, int(1.0 * 12) * 2); // 24
        CHECK(trailingSilent(hb, 3, steps - restBound), "space=1.0 protects larger trailing whitespace on boundary bar");
    }
}

// ===========================================================================
// MIDI EXPORT TESTS (only when Processor.h is available)
// ===========================================================================
#if __has_include("Processor.h")
#include "Processor.h"

struct TestTransport : juce::AudioPlayHead {
    double ppq=0, bpm=120; bool playing=true;
    juce::Optional<PositionInfo> getPosition() const override {PositionInfo p;p.setPpqPosition(ppq);p.setBpm(bpm);p.setIsPlaying(playing);return p;}
};
static void runTransportTests() {
    section("Processor: preview, host sync, stop and state");
    LatticeProcessor p; p.prepareToPlay(48000,256);TestTransport host;p.setPlayHead(&host);
    juce::AudioBuffer<float> buf(2,256);juce::MidiBuffer midi;float peak=0;
    for(int k=0;k<400;++k){host.ppq=k*256.0/24000.0;p.processBlock(buf,midi);peak=std::max(peak,buf.getMagnitude(0,256));}
    CHECK(peak>0 && peak<=1, "host PPQ transport produces bounded audio");
    host.playing=false;for(int k=0;k<500;++k)p.processBlock(buf,midi);
    CHECK(buf.getMagnitude(0,256)<0.00001f, "stopped host settles to silence");
    p.setPlayHead(nullptr);p.preview.store(true);peak=0;for(int k=0;k<50;++k){p.processBlock(buf,midi);peak=std::max(peak,buf.getMagnitude(0,256));}
    CHECK(peak>0, "internal preview produces audio without host");
    p.preview.store(false);for(int k=0;k<500;++k)p.processBlock(buf,midi);
    CHECK(buf.getMagnitude(0,256)<0.00001f, "preview stop settles to silence");
    p.pattern.meter(7,8);p.pattern.groups={3,2,2};p.pattern.generate();p.pattern.subdivide(2,1,0,3,3);p.pattern.bars[2][1][1].velocity=40;p.pattern.locked[1]=true;p.changed();
    juce::MemoryBlock state;p.getStateInformation(state);LatticeProcessor copy;copy.setStateInformation(state.getData(),int(state.getSize()));
    CHECK(copy.pattern.numerator==7 && copy.pattern.denominator==8 && copy.pattern.groups==p.pattern.groups, "state restores custom meter and grouping");
    CHECK(copy.pattern.locked[1] && copy.pattern.bars[2][1][1].start==80 && copy.pattern.bars[2][1][1].velocity==40, "state restores lock, triplet and ghost");
    p.releaseResources();
}
static void runProcessorMidiTests()
{
    g_usedProcessor = true;
    section("Processor: exportMidi -> juce::MidiFile (8 drums, 60-tick / 32nd grid)");
    {
        LatticeProcessor proc;
        {
            juce::ScopedLock lk(proc.modelLock);
            proc.pattern.generate();
            for (int r = 0; r < 8; ++r) proc.pattern.bars[0][r][0].velocity = 127;  // hit every lane at slot 0
            proc.pattern.subdivide(0, 1, 0, 7, 8);   // snare quarter -> 8 thirty-second notes
            proc.pattern.bars[0][1][2].velocity = 40; // snare @ tick 120, ghost velocity
        }
        juce::File f = proc.exportMidi();
        CHECK(f.existsAsFile(), "exportMidi() wrote a file");
        if (!f.existsAsFile()) return;

        juce::FileInputStream in(f);
        juce::MidiFile mf;
        CHECK(mf.readFrom(in), "MIDI file parses");
        CHECK(mf.getTimeFormat() == 480, "time format is 480 PPQ");

        std::set<int> pitches;
        int drumChannel = -1;
        bool hasTempo = false, hasTimeSig = false, tupletExact = false;
        double lastTick = 0.0;

        for (int t = 0; t < mf.getNumTracks(); ++t)
        {
            const auto* seq = mf.getTrack(t);
            if (seq == nullptr) continue;
            for (int i = 0; i < seq->getNumEvents(); ++i)
            {
                const auto& msg = seq->getEventPointer(i)->message;
                lastTick = std::max(lastTick, msg.getTimeStamp());
                if (msg.isNoteOn())
                {
                    pitches.insert(msg.getNoteNumber());
                    if (msg.getNoteNumber() == 38 && msg.getTimeStamp() == 120 && msg.getVelocity() == 40) tupletExact = true;
                    if (drumChannel < 0) drumChannel = msg.getChannel();   // 1-based
                }
                if (msg.isTempoMetaEvent())     hasTempo = true;
                if (msg.isTimeSignatureMetaEvent()) hasTimeSig = true;
            }
        }

        bool allGm = true;
        for (int r = 0; r < 8; ++r) if (!pitches.count(kGM[r])) allGm = false;
        CHECK(allGm, "all 8 GM pitches (36,38,37,42,46,45,51,49) present");
        CHECK(drumChannel == 10, "drum notes on channel 10");
        CHECK(tupletExact, "export preserves 60-tick (32nd) grid: snare @120 vel 40");
        CHECK(hasTempo, "tempo meta event present");
        CHECK(hasTimeSig, "time-signature meta event present");

        // 16 bars at 4/4, 480 PPQ => 16 * 4 * 480 = 30720 ticks.
        CHECK(lastTick >= 30600.0 && lastTick <= 30800.0, "sequence ends at 16-bar boundary (~30720 ticks)");
        std::printf("        (lastTick=%.0f, drumChannel=%d, tempo?=%d, timeSig?=%d)\n",
                    lastTick, drumChannel, (int)hasTempo, (int)hasTimeSig);

        f.deleteFile();   // clean up the temp export
    }
}

static void runStateVersionTests()
{
    g_usedProcessor = true;

    section("Processor: v2 state save/restore (16 bars, 8 tracks, new params)");
    {
        LatticeProcessor proc;
        {
            juce::ScopedLock lk(proc.modelLock);
            proc.pattern.meter(4, 4);
            proc.pattern.development = 0.3f;
            proc.pattern.fill = 0.7f;
            proc.pattern.space = 0.2f;
            proc.pattern.gridStep = 60;
            proc.pattern.barLocked[0] = true;
            proc.pattern.barLocked[15] = true;
            proc.pattern.locked[3] = true;
            proc.pattern.generate();
            proc.changed();
        }
        juce::MemoryBlock state; proc.getStateInformation(state);
        LatticeProcessor copy; copy.setStateInformation(state.getData(), (int)state.getSize());
        const auto& m = copy.pattern;
        CHECK(m.numerator == 4 && m.denominator == 4, "v2 restores meter");
        CHECK(std::abs(m.development - 0.3f) < 1e-3f
              && std::abs(m.fill - 0.7f) < 1e-3f
              && std::abs(m.space - 0.2f) < 1e-3f, "v2 restores development/fill/space");
        CHECK(m.gridStep == 60, "v2 restores gridStep=60");
        CHECK(m.barLocked[0] && m.barLocked[15] && !m.barLocked[1], "v2 restores barLocked flags");
        CHECK(m.locked[3] && !m.locked[0], "v2 restores track lock");
        bool identical = true;
        for (int b = 0; b < lattice::phraseBars && identical; ++b)
            for (int r = 0; r < lattice::tracks && identical; ++r)
            {
                const auto& la = proc.pattern.bars[b][r];
                const auto& lb = m.bars[b][r];
                if (la.size() != lb.size()) { identical = false; break; }
                for (size_t s = 0; s < la.size(); ++s)
                    if (la[s].velocity != lb[s].velocity
                        || la[s].start != lb[s].start
                        || la[s].length != lb[s].length) { identical = false; break; }
            }
        CHECK(identical, "v2 restores full 16x8 pattern exactly");
    }

    section("Processor: v1 state migration (4 bars / 7 tracks -> gridStep 120)");
    {
        // Build a v1 (version=1) document by hand and migrate via setStateInformation.
        // 4/4, groups 3+3+2 (sum 8 == eighths), 4 bars x 7 tracks.
        std::unique_ptr<juce::XmlElement> xml(new juce::XmlElement("LATTICE"));
        xml->setAttribute("version", 1);
        xml->setAttribute("n", 4); xml->setAttribute("d", 4);
        xml->setAttribute("bpm", 120); xml->setAttribute("seed", 7);
        xml->setAttribute("density", 0.6); xml->setAttribute("ghost", true); xml->setAttribute("accents", true);
        xml->setAttribute("groups", "3+3+2");
        int steps = 4 * 1920 / 4 / 60;   // 32 cells per 4/4 bar (60-tick grid)
        for (int b = 0; b < 4; ++b)
            for (int r = 0; r < 7; ++r)
            {
                auto* lane = xml->createNewChildElement("lane");
                lane->setAttribute("b", b); lane->setAttribute("r", r); lane->setAttribute("lock", r == 1);
                int pos = 0;
                for (int s = 0; s < steps; ++s)
                {
                    auto* c = lane->createNewChildElement("c");
                    c->setAttribute("t", pos); c->setAttribute("l", 60);
                    c->setAttribute("v", (s % 4 == 0) ? 80 : 0);
                    pos += 60;
                }
            }
        juce::MemoryBlock mb; juce::AudioProcessor::copyXmlToBinary(*xml, mb);
        LatticeProcessor proc; proc.setStateInformation(mb.getData(), (int)mb.getSize());
        const auto& m = proc.pattern;
        CHECK(m.numerator == 4 && m.denominator == 4, "v1 migrates meter 4/4");
        CHECK((m.groups == std::vector<int>{3, 3, 2}), "v1 migrates groups");
        CHECK(m.gridStep == 120, "v1 migrates to gridStep 120");
        CHECK(m.locked[1] && !m.locked[0], "v1 migrates track lock");
        CHECK(m.bars[0][0][0].velocity == 80 && m.bars[0][1][0].velocity == 80, "v1 restores lane velocities");
        CHECK(m.bars[0][7].empty() || m.bars[0][7][0].velocity == 0, "v1 leaves crash lane empty");
        bool upperEmpty = true;
        for (int b = 4; b < lattice::phraseBars; ++b)
            for (int r = 0; r < lattice::tracks; ++r)
                for (const auto& c : m.bars[b][r])
                    if (c.velocity != 0) upperEmpty = false;
        CHECK(upperEmpty, "v1 migration: bars 4..15 are empty (only 4 carried)");
    }
}
#endif

// ===========================================================================
// REAL VST3 LOAD / PROCESS / STATE
// ===========================================================================
static std::unique_ptr<juce::AudioPluginInstance>
loadVst3(const juce::String& vst3, juce::String& errOut)
{
    juce::File vf(vst3);
    if (!vf.exists()) { errOut = "file not found"; return nullptr; }

    juce::VST3PluginFormat fmt;
    juce::OwnedArray<juce::PluginDescription> descs;
    fmt.findAllTypesForFile(descs, vst3);
    if (descs.isEmpty()) { errOut = "no plugin type in file"; return nullptr; }

    // Synchronous helper: VST3 requiresUnblockedMessageThreadDuringCreation==false,
    // so this runs the creation directly on the calling (message) thread and is
    // safe without a running dispatch loop.
    return fmt.createInstanceFromDescription(*descs[0], 48000.0, 256, errOut);
}

static void runPluginLoadTest(const juce::String& vst3)
{
    section("VST3: load + prepareToPlay(48000/256) + 8 GM note-ons");
    juce::String err;
    auto instance = loadVst3(vst3, err);
    if (instance == nullptr)
        std::fprintf(stderr, "  vst3 create error: %s\n", err.toUTF8());
    CHECK(instance != nullptr, "createPluginInstance succeeded");
    if (instance == nullptr) return;

    instance->prepareToPlay(48000.0, 256);

    juce::AudioBuffer<float> buf(2, 256);
    for (int n = 0; n < 8; ++n)   // all 8 drums, including Crash (49)
    {
        instance->releaseResources();
        instance->prepareToPlay(48000.0, 256);
        buf.clear();
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(10, kGM[n], 1.0f), 0);     // channel 10 = GM drums
        midi.addEvent(juce::MidiMessage::noteOff(10, kGM[n], 0.0f), 200);
        instance->processBlock(buf, midi);

        bool finite = true; float peak = 0.0f;
        for (int blk = 0; blk < 3; ++blk)     // let the transient develop
        {
            juce::MidiBuffer silent;
            instance->processBlock(buf, silent);
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float v = buf.getSample(c, i);
                    if (!std::isfinite(v)) finite = false;
                    const float a = std::fabs(v);
                    if (a > peak) peak = a;
                }
        }
        CHECK(finite, "output finite for GM note");
        CHECK(peak > 0.0f, "output has nonzero peak for GM note");
        CHECK(peak <= 1.0f, "drum peak remains within full scale");
        recordPeak(peak);
        std::printf("        (GM %d -> peak %.4f)\n", kGM[n], peak);
    }

    section("VST3: state roundtrip");
    juce::MemoryBlock s1, s2;
    instance->getStateInformation(s1);
    CHECK(s1.getSize() > 0, "getStateInformation returned non-empty state");
    instance->setStateInformation(s1.getData(), (int)s1.getSize());
    instance->getStateInformation(s2);
    CHECK(s1 == s2, "state is stable across set->get roundtrip");

    instance->releaseResources();
    instance = nullptr;   // release the plugin
}

// ===========================================================================
// GUI TEST: open the real VST3 editor, snapshot to outputs/plugin-ui.png
// ===========================================================================
static void runGuiTest(const juce::String& vst3)
{
    section("GUI: real VST3 editor -> DocumentWindow -> screenshot");
    juce::String err;
    auto instance = loadVst3(vst3, err);
    if (instance == nullptr)
        std::fprintf(stderr, "  vst3 create error: %s\n", err.toUTF8());
    CHECK(instance != nullptr, "gui: instance created");
    if (instance == nullptr) return;

    instance->prepareToPlay(48000.0, 256);

    // createEditorIfNeeded() returns a NON-owning pointer: the processor owns and
    // deletes the active editor in its destructor, so we must not wrap it in a
    // unique_ptr. We just hold a raw observer pointer here.
    juce::AudioProcessorEditor* editor = instance->createEditorIfNeeded();
    CHECK(editor != nullptr, "gui: editor created");
    if (editor == nullptr) return;
    CHECK(editor->getWidth() > 0 && editor->getHeight() > 0, "gui: editor has non-zero size");

    juce::DocumentWindow win("ozo LATTICE", juce::Colours::white,
                             juce::DocumentWindow::allButtons, true);
    win.setContentNonOwned(editor, true);   // window does NOT own the editor
    const int w = editor->getWidth()  > 0 ? editor->getWidth()  : 900;
    const int h = editor->getHeight() > 0 ? editor->getHeight() : 640;
    win.setResizeLimits(w, h, w * 2, h * 2);
    win.setSize(w, h);
    win.addToDesktop();
    win.setVisible(true);
    win.toFront(true);

    juce::String outPath = "/Users/wangxuele/Documents/AI/Plug/LATTICE/outputs/plugin-ui.png";
    juce::File(outPath).getParentDirectory().createDirectory();
    std::printf("        (screenshot -> %s)\n",
                juce::File(outPath).getFullPathName().toUTF8());

    juce::Timer::callAfterDelay(1500, [ed = editor, outPath]()
    {
        bool shot = false;
        if (ed != nullptr && ed->isVisible() && ed->getWidth() > 0 && ed->getHeight() > 0)
        {
            LatticeProcessor visualProcessor;
            std::unique_ptr<juce::AudioProcessorEditor> visualEditor(visualProcessor.createEditor());
            juce::Image img = visualEditor->createComponentSnapshot(visualEditor->getLocalBounds());
            CHECK(img.getPixelAt(10,10).getBrightness()>0.5f, "native editor snapshot contains light UI pixels");
            if (img.isValid())
            {
                std::unique_ptr<juce::FileOutputStream> os(juce::File(outPath).createOutputStream());
                if (os != nullptr)
                {
                    os->setPosition(0); os->truncate();
                    juce::PNGImageFormat png;
                    shot = png.writeImageToStream(img, *os);
                    os->flush();
                }
            }
        }
        CHECK(shot, "gui: editor screenshot written (visible, size>0)");
        juce::MessageManager::getInstance()->stopDispatchLoop();
    });

    juce::MessageManager::getInstance()->runDispatchLoop();

    // Safe teardown order: detach the editor from the window, then release the
    // processor (its destructor deletes the active editor it owns).
    win.clearContentComponent();
    delete editor;
    instance = nullptr;
}

// ===========================================================================
// MAIN
// ===========================================================================
int main(int argc, char** argv)
{
    bool gui = false;
    juce::String vst3;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a = argv[i];
        if (a == "--gui")            gui = true;
        else if (a == "--vst3" && i + 1 < argc) vst3 = argv[++i];
        else if (!a.startsWith("--")) vst3 = a;   // positional .vst3 path
    }

    // Initialises the MessageManager + native GUI; lives until main returns so the
    // GUI/dispatch-loop test can use it.
    juce::ScopedJuceInitialiser_GUI guiInit;

    std::printf("=== LATTICE Tests ===\n");
    runModelTests();

#if __has_include("Processor.h")
    runProcessorMidiTests();
    runTransportTests();
    runStateVersionTests();
#else
    std::printf("\n[skip] Processor.h not found -> MIDI export / state tests inactive "
                "(they will compile once Processor.h is written).\n");
#endif

    if (vst3.isNotEmpty())
    {
        runPluginLoadTest(vst3);
        if (gui) runGuiTest(vst3);
    }
    else
    {
        std::printf("\n[note] Pass a .vst3 path (positional or --vst3) to run the "
                    "plugin load / process / state tests%s.\n",
                    gui ? " and the GUI screenshot" : "");
    }

    std::printf("\n=== SUMMARY ===\nPASS=%d  FAIL=%d  PEAK=%.4f%s\n",
                g_pass, g_fail, g_peak,
                g_usedProcessor ? "" : "  (processor MIDI/state tests skipped)");
    return g_fail > 0 ? 1 : 0;
}
