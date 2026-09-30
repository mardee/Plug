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
//    mapping (Crash = GM 49).
//  * 0.3.0：240 seed 两模式三档；组头 kick/hat/tom/rest，motif 正拍主击位移，
//    helper 共享固定源 10/35/65% 统计；dev=0 包括 Crash 精确重复；源发展仅
//    answer bars 位移。全部拍号、锁定、tuplets、静音源与冲突回归保持严格。
//  * 5+3 与 3+5 同 seed 不同句法；完整分组合法性与八轨覆盖矩阵。
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
//   LatticeTests --groove-only                   # 新groove/Tom + source + 独立Processor专项
//   LatticeTests --groove-audio-only --vst3 /path/to/ozoLATTICE.vst3
//                                               # 真实VST3完整16小节及Kick隔离，不写音频文件
// 专项通过不等于全量通过；默认全量仍执行旧generate风格断言，未静默跳过。
//   LatticeTests --wav-only                      # 仅后台 WAV 导出及按钮布局，不打开文件对话框
//   LatticeTests --editor-only                   # 仅编辑器控件专项
//   LatticeTests --editor-only --gui --vst3 /path/to/ozoLATTICE.vst3
//                                               # 仅 runGuiTest，不运行全量或采样测试
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
#include <algorithm>
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

#include "GroovePhraseTests.h"
#include "CoreRhythmTests.h"
#include "TomPhraseTests.h"

// GM drum note numbers, in lane order (matches lattice::notes, 8 lanes incl. Crash).
// Track 2 is Low Tom = GM 41 (0.2.5: Rim 37 was replaced by Low Tom 41).
static const int kGM[8] = { 36, 38, 41, 42, 46, 45, 51, 49 };

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

// 源与目标使用不同网格，避免把重新生成或只复制力度误判为完整复制。
static lattice::Pattern sourceDevelopmentFixture(int numerator = 4, int denominator = 4,
                                                  bool allVoices = false)
{
    lattice::Pattern p;
    p.meter(numerator, denominator);
    p.seed = 1729;
    p.fill = .7f;
    p.space = .6f;
    for (int b = 0; b < lattice::phraseBars; ++b)
        for (int r = 0; r < lattice::tracks; ++r)
        {
            auto& lane = p.bars[b][r];
            lane.clear();
            if (b != 0)
            {
                for (int t = 0; t < p.barTicks(); t += 120)
                    lane.push_back({t, 120, 81 + (b * 7 + r * 3 + t / 120) % 43});
                continue;
            }
            const bool audible = allVoices || (r != 2 && r != 4 && r != 6);
            for (int t = 0; t < p.barTicks();)
            {
                const int length = t < 120 ? 40 : 60;
                int velocity = 0;
                if (audible && t == (r % 3) * 40) velocity = 111 + r;
                if (audible && t == 180) velocity = 53 + r;
                if (audible && t == 600) velocity = 91 + r;
                if (audible && t == 720) velocity = 101 + r;
                if (audible && t == 1080) velocity = 29 + r;
                lane.push_back({t, length, velocity});
                t += length;
            }
        }
    return p;
}

static bool sourceLaneEqual(const lattice::Lane& a, const lattice::Lane& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].start != b[i].start || a[i].length != b[i].length
            || a[i].velocity != b[i].velocity) return false;
    return true;
}

static bool sourceBarsEqual(const lattice::Pattern& a, const lattice::Pattern& b)
{
    for (int bar = 0; bar < lattice::phraseBars; ++bar)
        for (int r = 0; r < lattice::tracks; ++r)
            if (!sourceLaneEqual(a.bars[bar][r], b.bars[bar][r])) return false;
    return true;
}

static std::array<bool, lattice::phraseBars> sourceAnswerBars(const lattice::Pattern& p)
{
    std::array<bool, lattice::phraseBars> answer{};
    std::mt19937 rng(static_cast<unsigned>(p.seed) ^ 0x42415231u);
    if (p.development > 0)
        for (int base : {0, 8})
        {
            std::array<int, 6> choices{{base + 1, base + 2, base + 3, base + 5, base + 6, base + 7}};
            std::shuffle(choices.begin(), choices.end(), rng);
            for (int i = 0; i < (p.development > .65f ? 2 : 1); ++i) answer[choices[i]] = true;
        }
    return answer;
}

static void runSourceDevelopmentTests()
{
    section("源小节发展：dev=0 精确复制三连音与非标准力度");
    for (bool sourceLocked : {false, true})
    {
        auto p = sourceDevelopmentFixture(4, 4, true);
        p.development = 0;
        p.barLocked[0] = sourceLocked;
        p.barLocked[6] = true;
        const auto before = p;
        CHECK(p.developFromBar1(), "dev=0 接受手工源，无论第 1 小节是否锁定");
        bool exact = true;
        for (int b = 0; b < lattice::phraseBars; ++b)
            for (int r = 0; r < lattice::tracks; ++r)
                exact &= sourceLaneEqual(p.bars[b][r], before.bars[p.barLocked[b] ? b : 0][r]);
        CHECK(exact, "全部未锁目标的 8 轨 cells 起点、长度、力度与源完全相同，锁小节不变");
    }

    section("源小节发展：多种子外围变化、主击保留与确定性");
    for (float dev : {.45f, .8f})
    {
        bool accepted = true, sourceKept = true, changed = true, mainsKept = true;
        bool emptyKept = true, gridKept = true, independent = true, repeatable = true;
        bool seedVaries = false;
        auto first = sourceDevelopmentFixture();
        for (int seed = 0; seed < 32; ++seed)
            for (bool sourceLocked : {false, true})
            {
                auto p = sourceDevelopmentFixture();
                p.seed = seed;
                p.development = dev;
                p.barLocked[0] = sourceLocked;
                const auto before = p;
                accepted &= p.developFromBar1();
                bool peripheralChange = false;
                const auto answers = sourceAnswerBars(before);
                int phraseMains = 0, phraseRetained = 0;
                for (int r = 0; r < lattice::tracks; ++r)
                {
                    const auto& source = before.bars[0][r];
                    sourceKept &= sourceLaneEqual(p.bars[0][r], source);
                    const bool silent = std::all_of(source.begin(), source.end(),
                        [](const lattice::Cell& c) { return c.velocity == 0; });
                    for (int b = 1; b < lattice::phraseBars; ++b)
                    {
                        const auto& lane = p.bars[b][r];
                        if (silent) emptyKept &= sourceLaneEqual(lane, source);
                        gridKept &= lane.size() == source.size();
                        int mains = 0, retained = 0;
                        for (const auto& c : source)
                        {
                            const auto hit = std::find_if(lane.begin(), lane.end(),
                                [&](const lattice::Cell& n) { return n.start == c.start; });
                            const bool sameCell = hit != lane.end() && hit->length == c.length;
                            gridKept &= sameCell;
                            if (c.velocity >= 80)
                            {
                                ++mains;
                                retained += sameCell && hit->velocity == c.velocity;
                            }
                            else if (sameCell && hit->velocity != c.velocity) peripheralChange = true;
                        }
                        if (!answers[b])
                        {
                            phraseMains += mains;
                            phraseRetained += retained;
                        }
                    }
                }
                mainsKept &= phraseMains > 0 && phraseRetained * 2 > phraseMains;
                changed &= peripheralChange;
                auto repeated = p;
                accepted &= repeated.developFromBar1();
                repeatable &= sourceBarsEqual(repeated, p);
                if (!sourceLocked)
                {
                    auto other = before;
                    // 目标改为完全不同的有效分格；源、种子和配置保持相同。
                    for (int b = 1; b < lattice::phraseBars; ++b)
                        for (int r = 0; r < lattice::tracks; ++r)
                            other.bars[b][r] = {{0, other.barTicks(), 17 + r}};
                    accepted &= other.developFromBar1();
                    independent &= sourceBarsEqual(other, p);
                    if (seed == 0) first = p;
                    else seedVaries |= !sourceBarsEqual(first, p);
                }
            }
        std::printf("        development=%.2f, 32 seeds x 2 source-lock states\n", double(dev));
        CHECK(accepted, "所有合法调用成功");
        CHECK(sourceKept, "第 1 小节锁定与否均逐 cell 保留");
        CHECK(changed, "每个种子均发展外围音符，而不是只重复源");
        CHECK(mainsKept, "整句非回答小节聚合保留多数源主击，回答小节允许位移");
        CHECK(emptyKept, "源空轨在全部目标中保持空轨，不被填充");
        CHECK(gridKept, "发展不改变源的 40tick 三连细分及其余 cell 起点和长度");
        CHECK(independent, "无锁时不同既有目标不影响相同源、种子、配置的结果");
        CHECK(repeatable, "对发展结果再次调用同 seed 得到完全一致的 16x8 cells");
        CHECK(seedVaries, "不同 seed 确实产生不同发展结果");
    }

    section("源小节发展：锁轨与锁小节逐 cell 保留");
    for (float dev : {0.f, .45f, .8f})
    {
        bool accepted = true, preserved = true, allLockedKept = true;
        for (int track = 0; track < lattice::tracks; ++track)
            for (int bar = 0; bar < lattice::phraseBars; ++bar)
            {
                auto p = sourceDevelopmentFixture();
                p.development = dev;
                p.locked[track] = true;
                p.barLocked[bar] = true;
                const auto before = p;
                accepted &= p.developFromBar1();
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                        if (b == 0 || b == bar || r == track)
                            preserved &= sourceLaneEqual(p.bars[b][r], before.bars[b][r]);
            }
        for (bool tracksLocked : {false, true})
        {
            auto p = sourceDevelopmentFixture();
            p.development = dev;
            p.locked.fill(tracksLocked);
            p.barLocked.fill(!tracksLocked);
            const auto before = p;
            accepted &= p.developFromBar1();
            allLockedKept &= sourceBarsEqual(p, before);
        }
        CHECK(accepted, "全部 8x16 锁轨/锁小节组合及全锁调用成功");
        CHECK(preserved, "锁轨的所有小节、锁小节的全部轨道及源完全保留");
        CHECK(allLockedKept, "全锁轨或全锁小节时整个 bars 不变");
    }

    section("源小节发展：非法输入事务性拒绝");
    auto rejectsUnchanged = [](lattice::Pattern p, const char* message)
    {
        const auto before = p;
        const bool rejected = !p.developFromBar1();
        CHECK(rejected && sourceBarsEqual(p, before), message);
    };
    {
        auto p = sourceDevelopmentFixture();
        for (auto& lane : p.bars[0])
            for (auto& c : lane) c.velocity = 0;
        rejectsUnchanged(p, "全静音源拒绝且原 bars 不变");
        for (auto& lane : p.bars[0]) lane.clear();
        rejectsUnchanged(p, "无 cell 的空源拒绝且原 bars 不变");
    }
    for (const auto& groups : std::vector<std::vector<int>>{{}, {7}, {0, 8}, {-1, 9}})
    {
        auto p = sourceDevelopmentFixture();
        p.groups = groups;
        rejectsUnchanged(p, "空分组、总和不符、零或负分组拒绝且原 bars 不变");
    }
    for (int r = 0; r < lattice::tracks; ++r)
        for (int defect = 0; defect < 9; ++defect)
        {
            auto p = sourceDevelopmentFixture();
            // 即便无效源位于锁轨/锁小节中，也必须先验证后提交。
            p.locked[r] = true;
            p.barLocked[0] = true;
            auto& lane = p.bars[0][r];
            switch (defect)
            {
                case 0: lane[0].start = -1; break;
                case 1: ++lane[1].start; break;
                case 2: --lane[1].start; break;
                case 3: lane[0].length = 0; break;
                case 4: lane[0].length = -40; break;
                case 5: ++lane.back().length; break;
                case 6: lane.pop_back(); break;
                case 7: lane[0].velocity = -1; break;
                case 8: lane[0].velocity = 128; break;
            }
            rejectsUnchanged(p, "各轨非法起点、间隙、重叠、长度、覆盖或力度拒绝且原 bars 不变");
        }

    section("源小节发展：全部 1..16 / 4、8 拍号安全");
    for (int n = 1; n <= 16; ++n)
        for (int d : {4, 8})
        {
            bool safe = true, exact = true, deterministic = true;
            for (float dev : {0.f, .45f, .8f})
                for (int seed : {0, 7, 1729})
                {
                    auto p = sourceDevelopmentFixture(n, d);
                    p.development = dev;
                    p.seed = seed;
                    const auto before = p;
                    safe &= p.valid() && p.developFromBar1();
                    for (int b = 0; b < lattice::phraseBars; ++b)
                        for (int r = 0; r < lattice::tracks; ++r)
                        {
                            int end = 0;
                            for (const auto& c : p.bars[b][r])
                            {
                                safe &= c.start == end && c.length > 0
                                    && c.length <= p.barTicks() - end
                                    && c.velocity >= 0 && c.velocity <= 127;
                                end += c.length;
                            }
                            safe &= end == p.barTicks();
                            if (dev == 0 || b == 0)
                                exact &= sourceLaneEqual(p.bars[b][r], before.bars[0][r]);
                        }
                    auto repeat = p;
                    safe &= repeat.developFromBar1();
                    deterministic &= sourceBarsEqual(p, repeat);
                }
            std::printf("        meter=%d/%d\n", n, d);
            CHECK(safe, "全部发展级别与种子：合法、连续、正长度、覆盖整小节且力度在范围内");
            CHECK(exact, "全部拍号保留源，dev=0 精确复制全部 8 轨");
            CHECK(deterministic, "全部拍号同 seed 重调用确定性");
        }
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
        CHECK(gm, "lanes map to GM drum notes {36,38,41,42,46,45,51,49}");
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

    section("Model: dev=0 两小节含 Crash 精确重复");
    {
        lattice::Pattern p; p.development=0; p.generate();
        bool exact = true;
        for (int b = 2; b < lattice::phraseBars; ++b)
            for (int r = 0; r < lattice::tracks; ++r)
                exact &= sourceLaneEqual(p.bars[b][r], p.bars[b % 2][r]);
        CHECK(exact, "dev=0：全部 16 小节含 Crash、Hat、Ride 逐 cell 重复源 motif");
        CHECK(p.bars[0][7][0].velocity == 127 && p.bars[2][7][0].velocity == 127,
              "Crash 随偶数源小节重复，不再特许额外 Crash 差异");
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
        auto sameIncludingCrash = [&](int x, int y)
        {
            for (int r = 0; r < lattice::tracks; ++r)
                if (!sourceLaneEqual(p.bars[x][r], p.bars[y][r])) return false;
            return true;
        };
        CHECK(eq(2,4) && eq(4,6) && eq(2,10) && eq(2,14), "motif[0] bars (2,4,6,10,14) identical");
        CHECK(eq(1,3) && eq(3,5) && eq(1,9), "motif[1] bars (1,3,5,9) identical");
        CHECK(sameIncludingCrash(0,2), "bar0 与 bar2 包括 Crash 逐 cell 相同");
        CHECK(sameIncludingCrash(8,10), "bar8 与 bar10 包括 Crash 逐 cell 相同");
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
        // dev=0 不再为后续段落另设更长休止；必须精确继承奇数源小节。
        lattice::Pattern hb; hb.development = 0; hb.fill = 0; hb.space = 1.0; hb.generate();
        const int restBound = std::min(steps - 1, int(1.0 * 6) * 2);
        bool exactRest = true;
        for (int r = 0; r < lattice::tracks; ++r)
            exactRest &= sourceLaneEqual(hb.bars[1][r], hb.bars[3][r]);
        CHECK(exactRest && trailingSilent(hb, 3, steps - restBound),
              "dev=0 space=1：bar3 精确重复 bar1，完整保留源句尾休止而不追加段落变化");
    }
}

// ---------------------------------------------------------------------------
// Grouping-semantics helpers (shared by the two grouping test functions below).
// ---------------------------------------------------------------------------
static int velAt(const lattice::Pattern& p, int bar, int track, int tick)
{
    const auto& lane = p.bars[bar][track];
    for (const auto& c : lane)
        if (tick >= c.start && tick < c.start + c.length) return c.velocity;
    return 0;
}

// Group-boundary slot indices, computed identically to Model::generate().
static std::vector<int> groupBoundaries(const std::vector<int>& groups)
{
    std::vector<int> b; int sum = 0;
    for (int g : groups) { b.push_back(sum * 4); sum += g; }
    return b;
}

// Answer (snare) slot, computed identically to Model::generate().
static int answerSlot(const std::vector<int>& bnd, int steps)
{
    return (bnd.size() > 1) ? bnd.back() : std::max(2, (steps / 2) / 2 * 2);
}

// Snare anchor, computed identically to Model::generate(): an independent
// position near steps/2 (seed-nudged, clamped, even) that belongs to the
// snare/tom sentence and is NOT a kick/cymbal group boundary.
static int snareAnchorStep(int seed, int steps)
{
    int a = steps / 2;
    if (steps >= 8)
    {
        a += ((seed % 3) - 1) * 2;
        a = std::clamp(a, 2, steps - 3);
        if (a % 2 != 0) a += (a + 1 <= steps - 3) ? 1 : -1;
    }
    return a;
}

// 正拍锚点可移至相邻十六分位置；击打数量由调用处单独验证。
static bool mainNear(const lattice::Pattern& p, int bar, int row, int tick, int velocity = 127)
{
    if (velAt(p, bar, row, tick) == velocity) return true;
    const int beat = p.denominator == 8 ? 240 : 480;
    if (tick % beat != 0) return false;
    for (int t : {tick - beat / 4, tick + beat / 4})
        if (t >= 0 && t < p.barTicks() && velAt(p, bar, row, t) == velocity) return true;
    return false;
}

// 稀疏无装饰夹具：精确验证 carrier 分配、休止、独立军鼓和允许的位移范围。
static bool sparseGroupingCorrect(const lattice::Pattern& p, int bar)
{
    std::array<std::vector<int>, 8> expected;
    std::mt19937 rng(static_cast<unsigned>(p.seed) ^ 0x43415252u);
    std::array<int, 4> carriers{{0, 3, -1, -1}};
    const auto boundaries = groupBoundaries(p.groups);
    const int anchor = snareAnchorStep(p.seed, p.barTicks() / 60) * 60;
    for (size_t i = 0; i < boundaries.size(); ++i)
    {
        if (i % 4 == 0) std::shuffle(carriers.begin(), carriers.end(), rng);
        const int r = carriers[i % 4], t = boundaries[i] * 60;
        if (r >= 0 && !(r == 0 && t == anchor) && !(r == 3 && t == 0 && bar % 2 == 0))
            expected[r].push_back(t);
    }
    expected[1].push_back(anchor);
    if (bar % 2 == 0) expected[7].push_back(0);
    for (int r = 0; r < lattice::tracks; ++r)
    {
        std::multiset<int> actual;
        for (const auto& c : p.bars[bar][r]) if (c.velocity)
        {
            if (c.velocity != 127) return false;
            actual.insert(c.start);
        }
        if (actual.size() != expected[r].size()) return false;
        for (int t : expected[r])
        {
            auto it = actual.find(t);
            if (it == actual.end() && (r == 0 || r == 1 || r == 5) && t % 480 == 0)
            {
                it = actual.find(t - 120);
                if (it == actual.end()) it = actual.find(t + 120);
            }
            if (it == actual.end()) return false;
            actual.erase(it);
        }
    }
    return true;
}

// ===========================================================================
// GROUPING SEMANTICS TESTS
// ===========================================================================
static void runGroupingSemanticsTests()
{
    // 相同 seed、无装饰条件比较分组句法；组头按 carrier/rest 分配，
    // 军鼓保持独立来源，但正拍锚点允许按档位移动。
    auto make = [](const std::vector<int>& g) {
        lattice::Pattern p;
        p.numerator = 4; p.denominator = 4;
        p.development = 0; p.fill = 0; p.space = 0;
        p.density = 0; p.ghost = false; p.accents = true;
        p.groups = g;
        p.generate();
        return p;
    };

    section("Model: grouping semantics — 5+3 vs 3+5 (same seed, dev0/fill0/space0/dens0/ghostOff/accents)");
    lattice::Pattern p53 = make({5, 3});
    lattice::Pattern p35 = make({3, 5});

    CHECK(p53.valid() && p35.valid(), "5+3 and 3+5 are both valid groupings (sum == 8 eighths)");

    // User grouping must really affect the sentence.
    bool identical = true;
    for (int b = 0; b < lattice::phraseBars && identical; ++b)
        for (int r = 0; r < lattice::tracks && identical; ++r)
        {
            const auto& la = p53.bars[b][r];
            const auto& lb = p35.bars[b][r];
            if (la.size() != lb.size()) { identical = false; break; }
            for (size_t s = 0; s < la.size(); ++s)
                if (la[s].velocity != lb[s].velocity) { identical = false; break; }
        }
    CHECK(!identical, "user grouping really changes the sentence (5+3 != 3+5)");

    // 精确验证稀疏 carrier/rest 计划，不把允许的留空误判为丢失主击。
    const int steps = p53.barTicks() / 60;
    const int anchor53 = snareAnchorStep(int(p53.seed), steps);
    const int anchor35 = snareAnchorStep(int(p35.seed), steps);

    auto groupKickHat = [&](const lattice::Pattern& p, const std::vector<int>& g, int bar) {
        return p.groups == g && sparseGroupingCorrect(p, bar);
    };

    CHECK(groupKickHat(p53, {5, 3}, 0), "5+3：bar0 精确符合 carrier/rest 与独立军鼓计划");
    CHECK(groupKickHat(p35, {3, 5}, 0), "3+5：bar0 精确符合 carrier/rest 与独立军鼓计划");
    CHECK(mainNear(p53, 0, 1, anchor53 * 60) && velAt(p53, 0, 0, anchor53 * 60) == 0,
          "5+3 snare main hit on its own anchor (not fixed 960, not group-derived 1200)");
    CHECK(mainNear(p35, 0, 1, anchor35 * 60) && velAt(p35, 0, 0, anchor35 * 60) == 0,
          "3+5 snare main hit on its own anchor (not fixed 960, not group-derived 720)");

    // Three-segment generation: kick/cymbal group heads + snare anchor are
    // protected/consistent across bars 0-2 (grouping still audible, anchor held).
    CHECK(groupKickHat(p53, {5, 3}, 0) && groupKickHat(p53, {5, 3}, 1) && groupKickHat(p53, {5, 3}, 2),
          "5+3 grouping (kick/hat) + snare anchor held across three segments (bars 0-2)");
    CHECK(groupKickHat(p35, {3, 5}, 0) && groupKickHat(p35, {3, 5}, 1) && groupKickHat(p35, {3, 5}, 2),
          "3+5 grouping (kick/hat) + snare anchor held across three segments (bars 0-2)");
}

// ===========================================================================
// GROUPING PARAMETER SWEEP (validity matrix + full track coverage)
// ===========================================================================
static void runGroupingSweepTests()
{
    section("Model: grouping parameter sweep — validity + full track coverage");

    struct Case { std::vector<int> groups; bool valid; };
    const Case cases[] = {
        {{8}, true},
        {{4, 4}, true},
        {{2, 2, 2, 2}, true},
        {{5, 3}, true},
        {{3, 5}, true},
        {{6, 2}, true},
        {{2, 6}, true},
        {{4, 2, 2}, true},
        {{2, 4, 2}, true},
        {{2, 2, 4}, true},
        {{3, 3, 2}, true},
        {{3, 2, 3}, true},
        {{2, 3, 3}, true},
        {{7, 1}, true},
        {{1, 7}, true},
        {{1, 1, 1, 1, 1, 1, 1, 1}, true},
        {{4, 3, 1}, true},
        {{1, 2, 2, 2, 1}, true},
        // invalid (rejected): sum != 8, contains 0, or empty
        {{5, 2}, false},
        {{9}, false},
        {{3, 3, 3}, false},
        {{0, 8}, false},
        {{}, false},
        {{2, 2, 2, 2, 2}, false},
    };

    int nValid = 0, nInvalid = 0;
    for (const auto& c : cases)
    {
        lattice::Pattern p;
        p.numerator = 4; p.denominator = 4;
        p.development = 0; p.fill = 0; p.space = 0;
        p.density = 0; p.ghost = false; p.accents = true;
        p.groups = c.groups;
        bool gen = p.generate();

        CHECK(p.valid() == c.valid, "valid() agrees with expected for grouping");
        CHECK(gen == c.valid, "generate() succeeds iff grouping is valid");

        if (!c.valid) { ++nInvalid; continue; }
        ++nValid;

        // Full track coverage: all 8 lanes fully generated (contiguous 60-tick
        // cells, full bar) for every bar.
        bool fullCoverage = true;
        int ticks = p.barTicks();
        for (int b = 0; b < lattice::phraseBars && fullCoverage; ++b)
            for (int r = 0; r < lattice::tracks && fullCoverage; ++r)
            {
                const auto& lane = p.bars[b][r];
                if (lane.empty()) { fullCoverage = false; break; }
                int pos = 0;
                for (const auto& cell : lane)
                {
                    if (cell.start != pos || cell.length != 60) { fullCoverage = false; break; }
                    pos += cell.length;
                }
                if (pos != ticks) fullCoverage = false;
            }
        CHECK(fullCoverage, "valid grouping: all 8 tracks fully covered (60-tick grid, full bar)");

        bool semantics = true;
        for (int b = 0; b < lattice::phraseBars; ++b)
            semantics &= sparseGroupingCorrect(p, b);
        CHECK(semantics, "合法分组：全部 16 小节精确符合 carrier/rest 计划及军鼓允许位移，不补击");
    }
    std::printf("        (valid=%d, invalid=%d groupings swept)\n", nValid, nInvalid);
}

// ===========================================================================
// OPEN / CLOSE HI-HAT LOGIC (new Model.h articulation)
// ---------------------------------------------------------------------------
// 开镲区间清除闭镲/Ride 并由闭镲收束；互斥、休止、锁定和收束验收保留。
// 0.3.0 只替换明确失效的“组头必有 carrier”要求，允许 tom 与 rest。
// ===========================================================================
static void runOpenCloseTests()
{
    // -----------------------------------------------------------------------
    // 60-seed default generation:
    //   * Open is non-zero (the open hi-hat actually fires);
    //   * opens are placed in the main body, not only at the tail slot;
    //   * no Open is ever parked inside the reserved rest region;
    //   * every even-bar open (which can never be a tail open) is followed by a
    //     closing Hat.
    // -----------------------------------------------------------------------
    section("Model: open/close hi-hat — 60-seed default behaviour");
    {
        const int seeds = 60;
        int openTotal    = 0;   // total Open hits discovered
        int evenOpenSeen = 0;   // opens found in even bars
        int closeFollows = 0;   // even-bar opens that are closed by a following Hat
        bool anyNotTail      = false;   // an open placed before the tail slot
        bool allBeforeStop   = true;    // no open inside the reserved rest region

        for (int si = 0; si < seeds; ++si)
        {
            lattice::Pattern p;
            p.seed = si + 1;     // vary the generative seed
            p.generate();
            const int steps = p.barTicks() / 60;

            for (int b = 0; b < lattice::phraseBars; ++b)
            {
                // Mirror Model::generate()'s rest computation for this bar.
                int rest = 0;
                if (p.development > 0 && b % 8 == 7)
                    rest = std::min(steps - 1, int(p.space * 12) * 2);
                else if (p.development == 0 && b % 2 == 1)
                    rest = std::min(steps - 1, int(p.space * 6) * 2);
                const int stop = steps - rest;

                for (const auto& c : p.bars[b][4])   // Open lane (track 4)
                {
                    if (c.velocity <= 0) continue;
                    ++openTotal;
                    const int onset = c.start / 60;
                    if (onset >= stop)      allBeforeStop = false;
                    if (onset < stop - 4)   anyNotTail    = true;

                    // Even bars never produce a tail open (rest == 0 there, so the
                    // style-3 branch degrades to a non-tail open). A close MUST exist.
                    if (b % 2 == 0)
                    {
                        ++evenOpenSeen;
                        bool hasClose = false;
                        for (int k = onset + 1; k < stop; ++k)
                            if (velAt(p, b, 3, k * 60) > 0) { hasClose = true; break; }
                        if (hasClose) ++closeFollows;
                    }
                }
            }
        }

        CHECK(openTotal > 0, "default generation produces Open hi-hat hits (60 seeds)");
        CHECK(anyNotTail,    "opens are placed in the main body, not only at the tail slot");
        CHECK(allBeforeStop, "no Open is ever placed inside the reserved rest region");
        CHECK(evenOpenSeen > 0 && closeFollows == evenOpenSeen,
              "every even-bar open is followed by a closing Hat (non-tail opens close)");
        std::printf("        (openTotal=%d, evenOpens=%d, closes=%d)\n",
                    openTotal, evenOpenSeen, closeFollows);
    }

    // -----------------------------------------------------------------------
    // Hat / Open / Ride are mutually exclusive at every tick (when unlocked).
    // -----------------------------------------------------------------------
    section("Model: Hat / Open / Ride mutually exclusive per tick (unlocked)");
    {
        const int seeds = 60;
        int violations = 0;
        for (int si = 0; si < seeds; ++si)
        {
            lattice::Pattern p;
            p.seed = si * 13 + 5;
            p.generate();
            const int steps = p.barTicks() / 60;
            for (int b = 0; b < lattice::phraseBars; ++b)
                for (int s = 0; s < steps; ++s)
                {
                    const int hat  = velAt(p, b, 3, s * 60);
                    const int open = velAt(p, b, 4, s * 60);
                    const int ride = velAt(p, b, 6, s * 60);
                    if ((hat > 0) + (open > 0) + (ride > 0) > 1) ++violations;
                }
        }
        CHECK(violations == 0,
              "no tick fires Hat + Open + Ride simultaneously (all 60 seeds)");
        if (violations) std::printf("        (violations=%d)\n", violations);
    }

    // -----------------------------------------------------------------------
    // The reserved rest region [stop, barTicks) is empty on all 8 tracks.
    // -----------------------------------------------------------------------
    section("Model: reserved rest region empty on all 8 tracks");
    {
        const int seeds = 60;
        bool restEmpty = true;
        int offending = 0;
        for (int si = 0; si < seeds; ++si)
        {
            lattice::Pattern p;
            p.seed = si * 7 + 3;
            p.generate();
            const int steps = p.barTicks() / 60;
            for (int b = 0; b < lattice::phraseBars; ++b)
            {
                int rest = 0;
                if (p.development > 0 && b % 8 == 7)
                    rest = std::min(steps - 1, int(p.space * 12) * 2);
                else if (p.development == 0 && b % 2 == 1)
                    rest = std::min(steps - 1, int(p.space * 6) * 2);
                const int stop = steps - rest;
                for (int r = 0; r < lattice::tracks; ++r)
                    for (int s = stop; s < steps; ++s)
                        if (velAt(p, b, r, s * 60) > 0) { restEmpty = false; ++offending; }
            }
        }
        CHECK(restEmpty, "reserved rest region [stop,barTicks) empty on all 8 tracks (60 seeds)");
        if (offending) std::printf("        (%d offending cells)\n", offending);
    }

    // -----------------------------------------------------------------------
    // Same seed -> byte-identical pattern (reproducibility, 60 seeds).
    // -----------------------------------------------------------------------
    section("Model: same seed reproduces identical pattern (60 seeds)");
    {
        bool reproducible = true;
        int bad = 0;
        for (int si = 0; si < 60; ++si)
        {
            const int sd = si * 101 + 1;
            lattice::Pattern a, b;
            a.seed = sd; a.generate();
            b.seed = sd; b.generate();
            bool same = (a.bars.size() == b.bars.size());
            for (int bb = 0; bb < lattice::phraseBars && same; ++bb)
                for (int r = 0; r < lattice::tracks && same; ++r)
                {
                    const auto& la = a.bars[bb][r];
                    const auto& lb = b.bars[bb][r];
                    if (la.size() != lb.size()) { same = false; break; }
                    for (size_t i = 0; i < la.size(); ++i)
                        if (la[i].start != lb[i].start || la[i].length != lb[i].length
                            || la[i].velocity != lb[i].velocity) { same = false; break; }
                }
            if (!same) { reproducible = false; ++bad; }
        }
        CHECK(reproducible, "same seed -> byte-identical pattern across 60 seeds");
        if (bad) std::printf("        (%d non-reproducible seeds)\n", bad);
    }

    // -----------------------------------------------------------------------
    // development=0：完整重复装饰后的源句，包括 Crash 及其镲避让。
    // -----------------------------------------------------------------------
    section("Model: dev=0 开闭镲与 Crash 一起精确重复");
    {
        lattice::Pattern p;
        p.development = 0;
        p.generate();
        bool match = true;
        for (int r = 0; r < lattice::tracks; ++r)
            match &= sourceLaneEqual(p.bars[0][r], p.bars[2][r]);
        CHECK(match, "dev=0：bar0 与 bar2 不再允许 Crash 或首拍镲差异");
        CHECK(p.bars[0][7][0].velocity == 127 && p.bars[2][7][0].velocity == 127,
              "dev=0：Crash 在 bar0 与 bar2 均保留");
    }

    // -----------------------------------------------------------------------
    // Independently locking Hat (track 3) or Open (track 4) leaves that lane
    // untouched by regenerate(); only the unlocked cymbals are rewritten.
    // -----------------------------------------------------------------------
    section("Model: independently locked Hat / Open lanes are not overwritten");
    {
        // Lock Hat only, then pre-set a distinctive Hat bar and regenerate.
        lattice::Pattern ph;
        ph.generate();
        ph.locked[3] = true;
        ph.bars[0][3].clear();
        ph.bars[0][3].push_back({0, 60, 99});
        ph.bars[0][3].push_back({60, 60, 88});
        ph.seed = 4242; ph.generate();
        const bool hatOk = (ph.bars[0][3].size() == 2
                            && ph.bars[0][3][0].start == 0  && ph.bars[0][3][0].velocity == 99
                            && ph.bars[0][3][1].start == 60 && ph.bars[0][3][1].velocity == 88);
        CHECK(hatOk, "locked Hat lane (track 3) keeps its pre-set cells after regenerate");

        // Lock Open only, then pre-set a distinctive Open bar and regenerate.
        lattice::Pattern po;
        po.generate();
        po.locked[4] = true;
        po.bars[0][4].clear();
        po.bars[0][4].push_back({0, 60, 77});
        po.seed = 999; po.generate();
        const bool openOk = (po.bars[0][4].size() == 1
                             && po.bars[0][4][0].start == 0 && po.bars[0][4][0].velocity == 77);
        CHECK(openOk, "locked Open lane (track 4) keeps its pre-set cell after regenerate");
    }

    // -----------------------------------------------------------------------
    // groups 5+3 -> the main snare answer at tick 1200 still lands in bar 0
    // even with the open hi-hat generation active (the open must not eat it).
    // -----------------------------------------------------------------------
    section("Model: groups 5+3 -> grouping shown by kick/cymbal; snare on its own anchor (open gen on)");
    {
        lattice::Pattern p;
        p.numerator = 4; p.denominator = 4;
        p.groups = { 5, 3 };          // default density => open generation ON
        p.generate();
        const int steps = p.barTicks() / 60;
        const int anchor = snareAnchorStep(int(p.seed), steps);
        // The 5+3 boundary (step 20 = tick 1200) is articulated by the KICK (and
        // hat) so the grouping stays audible even with the open hi-hat firing; the
        // open logic never touches the kick, so this head is robust.
        auto sparse = p; sparse.density = 0; sparse.development = 0;
        sparse.fill = sparse.space = 0; sparse.ghost = false; sparse.generate();
        bool inherited = true;
        for (int r : {0, 5})
            for (const auto& c : sparse.bars[0][r]) if (c.velocity == 127)
                inherited &= velAt(p, 0, r, c.start) == c.velocity;
        CHECK(inherited && sparseGroupingCorrect(sparse, 0),
              "5+3：开闭镲不破坏稀疏源计划中的 kick/tom 主击，组头允许 rest");
        // The snare answers on its OWN independent anchor (kick cleared there); it
        // is neither at the group-derived 1200 nor at a fixed 960.
        CHECK(mainNear(p, 0, 1, anchor * 60), "5+3：军鼓来自独立锚点，允许合法位移");
        CHECK(velAt(p, 0, 0, anchor * 60) == 0,   "5+3: kick cleared at the snare anchor");
    }

    // -----------------------------------------------------------------------
    // 开镲开启时组头仍必须满足镲互斥；carrier/rest 的精确分配另由稀疏夹具检查。
    // -----------------------------------------------------------------------
    section("Model: OPEN 开启时组头的 Crash 与镲互斥回归（允许 rest）");
    {
        auto hatHeadsStrict = [&](const lattice::Pattern& p, const std::vector<int>& g, int bar) -> bool
        {
            for (int s : groupBoundaries(g))
            {
                const int tick = s * 60;
                if (s == 0) { if (velAt(p, bar, 7, tick) != 127 || velAt(p, bar, 3, tick) != 0) return false; }
                else { // A group head can be carried by a drum while the hat opens across it.
                    // 组头允许 tom 或 rest；不再要求强 carrier，镲互斥仍严格验证。
                    int cymbals=0;for(int r:{3,4,6})if(velAt(p,bar,r,tick)>0)++cymbals;
                    if(cymbals>1)return false;
                }
            }
            return true;
        };

        const std::vector<int> groups[] = {
            {3,3,2}, {5,3}, {3,5}, {4,4}, {2,2,2,2}, {6,2}, {2,6}
        };
        bool allPass = true;
        int checked = 0, failed = 0;
        for (const auto& gg : groups)
        {
            for (int si = 0; si < 60; ++si)
            {
                lattice::Pattern p;
                p.numerator = 4; p.denominator = 4;
                p.groups = gg;                 // default density -> open generation ON
                p.seed = si * 17 + 11;
                p.generate();
                ++checked;
                if (!hatHeadsStrict(p, gg, 0)) { allPass = false; ++failed; }
            }
        }
        CHECK(allPass, "组头允许 rest，但 Crash 首拍及 Hat/Open/Ride 互斥仍成立");
        std::printf("        (checked=%d groupings x seeds, failures=%d)\n", checked, failed);
    }
}

// ===========================================================================
// BACKBEAT MODEL TESTS (header-only Model, no Processor required)
// ---------------------------------------------------------------------------
// The backbeat articulation is a straight 4/4 pulse: snare on beats 2 & 4
// (ticks 480 / 1440), kick on the down-beat and the "and" of 3 (ticks 0 / 960,
// plus a soft 80 at 1200).  After all phrase edits the strong kick/snare unison
// resolver runs: where both land >= 80 at the same slot, bar 0 keeps the kick
// and every other bar keeps the snare, except a locked track always wins.  The
// tests below pin that behaviour without relaxing any assertion.
// ===========================================================================
static void runBackbeatTests()
{
    // -----------------------------------------------------------------------
    // 60 seeds x 16 bars: kick/snare are strictly mutually exclusive, the
    // backbeat anchor hits land where they should, and BPM is untouched.
    // -----------------------------------------------------------------------
    section("Model: backbeat — 60 seeds, all 16 bars, kick/snare mutual exclusion");
    {
        int snareOk = 0, kickOk = 0, bpmOk = 0, mutualOk = 1;
        for (int si = 0; si < 60; ++si)
        {
            lattice::Pattern p;
            p.seed = si * 11 + 3;             // vary the generative seed
            p.backbeat = true;
            p.development = 0; p.fill = 0; p.space = 0;   // long-phrase off, no fill/rest
            p.generate();
            if (std::abs(p.bpm - 100.0) < 1e-9) ++bpmOk;

            // 位移允许改变起点，不允许复制或丢失 backbeat 原有重击。
            int snareAccents = 0, kickAccents = 0;
            for (const auto& c : p.bars[0][1]) snareAccents += c.velocity == 127;
            for (const auto& c : p.bars[0][0]) kickAccents += c.velocity == 127;
            if (snareAccents == 2 && mainNear(p, 0, 1, 480) && mainNear(p, 0, 1, 1440)) ++snareOk;
            if (kickAccents == 3 && mainNear(p, 0, 0, 0) && mainNear(p, 0, 0, 960)
                && velAt(p, 0, 0, 1200) >= 80) ++kickOk;

            // KICK and SNARE must never fire at the same tick (all 16 bars).
            const int steps = p.barTicks() / 60;
            for (int b = 0; b < lattice::phraseBars && mutualOk; ++b)
                for (int s = 0; s < steps && mutualOk; ++s)
                    if (velAt(p, b, 0, s * 60) > 0 && velAt(p, b, 1, s * 60) > 0) mutualOk = 0;
        }
        CHECK(snareOk == 60, "backbeat 军鼓 480/1440 保留原位或合法十六分位移（60 seeds）");
        CHECK(kickOk  == 60, "backbeat kick 0/960 允许位移，1200 偏拍主击仍原位保留（60 seeds）");
        CHECK(bpmOk   == 60, "backbeat leaves BPM at default 100 (60 seeds)");
        CHECK(mutualOk == 1, "backbeat: kick & snare never overlap at any tick (16 bars x 60 seeds)");
    }

    // -----------------------------------------------------------------------
    // Locked-track priority on a forced kick/snare unison.
    //   * single lock: the locked kick wins, the unlocked snare is removed;
    //   * double lock: both locked hits survive untouched.
    // We build the natural backbeat kick lane as the locked material so the rest
    // of the groove is preserved, then force a kick at the snare's 480 slot.
    // -----------------------------------------------------------------------
    section("Model: backbeat — locked track wins on kick/snare conflict; double lock keeps both");
    {
        lattice::Pattern ref;
        ref.backbeat = true; ref.development = 0; ref.fill = 0; ref.space = 0; ref.generate();

        // Single lock: kick locked, forced kick at snare's 480 slot.
        lattice::Pattern p;
        p.backbeat = true; p.development = 0; p.fill = 0; p.space = 0;
        p.locked[0] = true;                       // lock the kick
        p.bars[0][0] = ref.bars[0][0];            // full backbeat kick pattern
        for(auto& c:p.bars[0][0])if(c.start==480)c.velocity=127; // valid locked lane
        p.generate();
        CHECK(velAt(p, 0, 0, 480) == 127, "single lock: locked kick @480 kept despite snare conflict");
        CHECK(velAt(p, 0, 1, 480) == 0,   "single lock: unlocked snare @480 removed where kick wins");
        CHECK(mainNear(p, 0, 1, 1440), "单锁：1440 军鼓主击保留或合法位移");
        auto expectedKick = ref.bars[0][0];
        for (auto& c : expectedKick) if (c.start == 480) c.velocity = 127;
        CHECK(sourceLaneEqual(p.bars[0][0], expectedKick), "单锁：包括已位移主击在内的整条 kick 精确保留");

        // Double lock: both kick and snare locked at 480 -> both preserved.
        lattice::Pattern q;
        q.backbeat = true; q.development = 0; q.fill = 0; q.space = 0;
        q.locked[0] = true; q.locked[1] = true;
        q.bars[0][0].clear(); q.bars[0][0].push_back({480, 60, 127});
        q.bars[0][1].clear(); q.bars[0][1].push_back({480, 60, 127});
        q.generate();
        CHECK(velAt(q, 0, 0, 480) == 127 && velAt(q, 0, 1, 480) == 127,
              "double lock: both locked kick & snare @480 preserved on conflict");
    }
}

// ===========================================================================
// BACKBEAT DEVELOPMENT MODEL TESTS (header-only Model, no Processor required)
// ---------------------------------------------------------------------------
// With backbeat + development (.45 / .8) the model answers the fixed 2/4 anchor
// with four related gestures (establish / early / late / withheld-let-toms):
// distinct snare main-hit position sets, varying accents, and a toms answer
// development=0 重复已经位移的 motif（由 runBackbeatTests 与三档测试覆盖）。
// 下列发展验收保留 60 seed 覆盖。
// ===========================================================================
static void runBackbeatDevelopmentModelTests()
{
    // 60 seeds x {development .45, .8}: at least two distinct snare main-hit
    // position sets, velocity variation, and no strong kick/snare unison.
    for (float dev : { 0.45f, 0.8f })
    {
        section(("Model: backbeat + development=" + std::to_string(dev)
                 + " — 60 seeds, >=2 snare sets, velocity change, kick/snare separation").c_str());
        std::set<std::string> snareSets;
        std::set<int> snareVels;
        bool noConflict = true, everyPhraseVaries = true;
        for (int si = 0; si < 60; ++si)
        {
            lattice::Pattern p;
            p.seed = si * 11 + 3;
            p.backbeat = true;
            p.development = dev; p.fill = 0; p.space = 0;   // long-phrase on, no fill/rest
            p.generate();
            std::set<std::string> positions;
            for(int b=0;b<16;++b){std::string pos;for(int t=0;t<p.barTicks();t+=60)if(velAt(p,b,1,t)>=80)pos+=std::to_string(t)+",";positions.insert(pos);}
            everyPhraseVaries &= positions.size()>=2;
            const int steps = p.barTicks() / 60;
            for (int b = 0; b < lattice::phraseBars; ++b)
            {
                std::string key;
                for (int s = 0; s < steps; ++s)
                {
                    const int sv = velAt(p, b, 1, s * 60);   // snare (track 1)
                    const int kv = velAt(p, b, 0, s * 60);   // kick  (track 0)
                    if (sv >= 80) { key += std::to_string(s) + ":" + std::to_string(sv) + ";";
                                    snareVels.insert(sv); }
                    if (kv >= 80 && sv >= 80) noConflict = false;
                }
                if (!key.empty()) snareSets.insert(key);
            }
        }
        CHECK(everyPhraseVaries,"each seed develops snare positions within its full sixteen-bar phrase");
        CHECK(snareSets.size() >= 2,
              "backbeat+development: >=2 distinct snare main-hit position/velocity sets across 60 seeds");
        CHECK(!snareVels.empty() && *snareVels.begin() < 127,
              "backbeat+development: snare accents vary (non-127 velocity present, e.g. 80/40)");
        CHECK(noConflict, "backbeat+development: no simultaneous strong kick/snare across 60 seeds");
    }

    // fill=1, space=0 across 60 seeds: both Low Tom (track 2, note 41) and Tom
    // (track 5) are generated somewhere in the phrase.
    section("Model: backbeat + fill=1 space=0 — 60 seeds generate both Low Tom (41) and Tom");
    {
        bool lowTom = false, tom = false;
        for (float dev : { 0.45f, 0.8f })
            for (int si = 0; si < 60; ++si)
            {
                lattice::Pattern p;
                p.seed = si * 11 + 3;
                p.backbeat = true;
                p.development = dev; p.fill = 1; p.space = 0;
                p.generate();
                for (int b = 0; b < lattice::phraseBars; ++b)
                {
                    for (const auto& c : p.bars[b][2]) if (c.velocity > 0) lowTom = true;  // Low Tom (41)
                    for (const auto& c : p.bars[b][5]) if (c.velocity > 0) tom = true;      // Tom
                }
            }
        CHECK(lowTom, "backbeat fill=1 space=0: Low Tom (track 2, MIDI 41) generated across seeds");
        CHECK(tom,    "backbeat fill=1 space=0: Tom (track 5) generated across seeds");
    }
}

// ===========================================================================
// Ride 规划回归：只统计生成结果，不复刻内部 RNG 或指定 seed 对应的方案。
// ===========================================================================
static void runRidePlanningTests()
{
    const int seeds = 120;
    auto sameLane = [](const lattice::Lane& a, const lattice::Lane& b)
    {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i)
            if (a[i].start != b[i].start || a[i].length != b[i].length
                || a[i].velocity != b[i].velocity) return false;
        return true;
    };

    for (bool backbeat : { false, true })
        for (float dev : { 0.45f, 0.8f })
        {
            section(("Model: Ride 规划 — 120 seeds, backbeat=" + std::to_string(backbeat)
                     + ", development=" + std::to_string(dev) + ", fill=0 space=0").c_str());
            std::set<int> entries;
            std::set<std::string> distributions;
            int generated = 0, withRide = 0, withoutRide = 0, separated = 0, partial = 0;
            int changedLanes = 0, conflicts = 0, missingHeads = 0, heads = 0;
            for (int seed = 1; seed <= seeds; ++seed)
            {
                lattice::Pattern p;
                p.seed = seed; p.backbeat = backbeat;
                p.development = dev; p.fill = 0; p.space = 0;
                if (p.generate()) ++generated;
                const auto before = p.bars;
                if (p.generate()) ++generated;
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                        if (!sameLane(before[b][r], p.bars[b][r])) ++changedLanes;

                int first = -1, runs = 0;
                bool previousRideBar = false, partialBar = false;
                std::string distribution;
                for (int b = 0; b < lattice::phraseBars; ++b)
                {
                    bool rideBar = false, hatBeforeRide = false;
                    for (int t = 0; t < p.barTicks(); t += 60)
                    {
                        const int hat = velAt(p, b, 3, t), open = velAt(p, b, 4, t);
                        const int ride = velAt(p, b, 6, t);
                        if ((hat > 0) + (open > 0) + (ride > 0) > 1) ++conflicts;
                        if (ride > 0)
                        {
                            if (first < 0) first = b * p.barTicks() + t;
                            if (hatBeforeRide) partialBar = true;
                            rideBar = true;
                        }
                        else if (!rideBar && (hat > 0 || open > 0)) hatBeforeRide = true;
                    }
                    // 以有 Ride 的小节分布计数，避免把单纯力度变化当成新方案。
                    distribution += rideBar ? '1' : '0';
                    if (rideBar && !previousRideBar) ++runs;
                    previousRideBar = rideBar;
                    int tick = 0;
                    for (int group : p.groups)
                    {
                        bool carrier = false;
                        for (int r = 0; r < lattice::tracks; ++r)
                            if (velAt(p, b, r, tick) >= 80) carrier = true;
                        ++heads;
                        if (!carrier) ++missingHeads;
                        tick += group * (lattice::ppq / 2);
                    }
                }
                if (first >= 0)
                {
                    ++withRide;
                    entries.insert(first);
                    distributions.insert(distribution);
                }
                else ++withoutRide;
                // 两段之间必须至少隔一个完全没有 Ride 的小节。
                if (runs >= 2) ++separated;
                // 同一小节先有 Hat/Open 再有 Ride，排除仅因 Crash 清掉首拍的情况。
                if (partialBar) ++partial;
            }
            std::printf("        actual count: generated=%d/%d, entries=%zu, distributions=%zu, "
                        "withRide=%d, withoutRide=%d, separated=%d, partial=%d, "
                        "changedLanes=%d, conflicts=%d, missingHeads=%d/%d\n",
                        generated, seeds * 2, entries.size(), distributions.size(), withRide,
                        withoutRide, separated, partial, changedLanes, conflicts, missingHeads, heads);
            CHECK(generated == seeds * 2, "120 seeds：首次生成及相同 seed 重生成均成功");
            CHECK(entries.size() >= 5, "Ride 首次进入位置至少 5 种");
            CHECK(distributions.size() >= 8, "有 Ride 的小节分布至少 8 种（不计无 Ride 方案）");
            CHECK(withRide > 0, "存在有 Ride 的方案");
            CHECK(withoutRide > 0, "存在无 Ride 的方案");
            CHECK(separated > 0, "存在以无 Ride 小节隔开的至少两段 Ride");
            CHECK(partial > 0, "存在同一小节从 Hat/Open 转入 Ride 的局部编配");
            CHECK(changedLanes == 0, "相同 seed 重生成后所有 Cell 的起点、长度、力度一致");
            CHECK(conflicts == 0, "Hat/Open/Ride 在全部 60-tick 网格位置互斥");
            CHECK(heads > 0 && missingHeads > 0 && missingHeads < heads,
                  "Ride 规划不强行补满全部组头，同时仍保留有强击的组头");
        }

    for (bool backbeat : { false, true })
    {
        section(("Model: Ride development=0 — 120 seeds, backbeat="
                 + std::to_string(backbeat)).c_str());
        int generated = 0, rideHits = 0, changedLanes = 0;
        for (int seed = 1; seed <= seeds; ++seed)
        {
            lattice::Pattern p;
            p.seed = seed; p.backbeat = backbeat;
            p.development = 0; p.fill = 0; p.space = 0;
            if (p.generate()) ++generated;
            for (int b = 0; b < lattice::phraseBars; ++b)
            {
                for (const auto& c : p.bars[b][6]) if (c.velocity > 0) ++rideHits;
                for (int r = 0; r < lattice::tracks; ++r)
                {
                    const auto& expected = p.bars[b % 2][r];
                    // dev=0 连同 Crash 点缀一起重复，不允许任何额外差异。
                    if (!sameLane(expected, p.bars[b][r])) ++changedLanes;
                }
            }
        }
        std::printf("        actual count: generated=%d/%d, rideHits=%d, changedLanes=%d\n",
                    generated, seeds, rideHits, changedLanes);
        CHECK(generated == seeds, "dev=0：120 seeds 均生成成功");
        CHECK(rideHits == 0, "dev=0：不会新增 Ride");
        CHECK(changedLanes == 0, "dev=0：全部 16 小节逐 cell 重复两小节源句，包括 Crash");
    }

    for (bool backbeat : { false, true })
        for (float dev : { 0.45f, 0.8f })
        {
            for (int lockedTrack : { 3, 4, 6 })
            {
                section(("Model: cymbal 单轨锁 — 120 seeds, track=" + std::to_string(lockedTrack)
                         + ", backbeat=" + std::to_string(backbeat)
                         + ", development=" + std::to_string(dev)).c_str());
                int generated = 0, changedLanes = 0, lockedHits = 0, conflicts = 0;
                for (int seed = 1; seed <= seeds; ++seed)
                {
                    lattice::Pattern p;
                    p.seed = seed; p.backbeat = backbeat;
                    p.development = dev; p.fill = 0; p.space = 0;
                    p.locked[lockedTrack] = true;
                    for (int b = 0; b < lattice::phraseBars; ++b)
                    {
                        // 非默认长度、力度、空格与稀疏轨道，检查完整保留而非仅比较 hit。
                        p.bars[b][lockedTrack] = {{0, 120, 93}, {120, 60, 0},
                                                {720, 30, 41}, {1440, 90, 109}};
                    }
                    const auto before = p.bars;
                    if (p.generate()) ++generated;
                    for (int b = 0; b < lattice::phraseBars; ++b)
                    {
                        if (!sameLane(before[b][lockedTrack], p.bars[b][lockedTrack])) ++changedLanes;
                        for (const auto& c : before[b][lockedTrack]) if (c.velocity > 0)
                        {
                            ++lockedHits;
                            for (int r : { 3, 4, 6 })
                                if (r != lockedTrack && velAt(p, b, r, c.start) > 0) ++conflicts;
                        }
                    }
                }
                std::printf("        actual count: generated=%d/%d, changedLanes=%d, "
                            "lockedHits=%d, conflicts=%d\n",
                            generated, seeds, changedLanes, lockedHits, conflicts);
                CHECK(generated == seeds, "单独锁定 cymbal：120 seeds 均生成成功");
                CHECK(changedLanes == 0, "锁定轨的全部 Cell 起点、长度、力度及数量保持不变");
                CHECK(lockedHits > 0 && conflicts == 0, "其他生成的 cymbal 避让锁定轨的所有 hit 起点");
            }

            section(("Model: barLocked 与 Ride 句末休止 — 120 seeds, backbeat="
                     + std::to_string(backbeat) + ", development=" + std::to_string(dev)).c_str());
            int generatedLocked = 0, changedBars = 0, lockedBars = 0;
            int generatedRest = 0, restTicks = 0, restRide = 0, restOther = 0, activeRide = 0;
            for (int seed = 1; seed <= seeds; ++seed)
            {
                lattice::Pattern p;
                p.seed = seed; p.backbeat = backbeat;
                p.development = dev; p.fill = 1; p.space = 1;
                // 同时锁定中段与句末，连休止区域内的手工内容也必须原样保留。
                for (int b : { 3, 7, 12, 15 })
                {
                    p.barLocked[b] = true;
                    for (int r = 0; r < lattice::tracks; ++r)
                        p.bars[b][r] = {{r * 30, 30 + r, 91 + r}, {480, 75, 0},
                                        {p.barTicks() - 60, 60, 53 + r}};
                    p.bars[b][2].clear();
                }
                const auto before = p.bars;
                if (p.generate()) ++generatedLocked;
                for (int b : { 3, 7, 12, 15 })
                {
                    ++lockedBars;
                    bool same = true;
                    for (int r = 0; r < lattice::tracks; ++r)
                        if (!sameLane(before[b][r], p.bars[b][r])) same = false;
                    if (!same) ++changedBars;
                }

                p.barLocked.fill(false);
                if (p.generate()) ++generatedRest;
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (const auto& c : p.bars[b][6]) if (c.velocity > 0) ++activeRide;
                // 4/4、space=1 的句末后三拍是休止区，fill=1 不应回填。
                for (int b : { 7, 15 })
                    for (int t = lattice::ppq; t < p.barTicks(); t += 60)
                    {
                        ++restTicks;
                        if (velAt(p, b, 6, t) > 0) ++restRide;
                        for (int r = 0; r < lattice::tracks; ++r)
                            if (r != 6 && velAt(p, b, r, t) > 0) ++restOther;
                    }
            }
            std::printf("        actual count: generatedLocked=%d/%d, changedBars=%d/%d, "
                        "generatedRest=%d/%d, restTicks=%d, restRide=%d, restOther=%d, activeRide=%d\n",
                        generatedLocked, seeds, changedBars, lockedBars, generatedRest, seeds,
                        restTicks, restRide, restOther, activeRide);
            CHECK(generatedLocked == seeds, "barLocked：120 seeds 均生成成功");
            CHECK(lockedBars > 0 && changedBars == 0, "barLocked 保留全部八轨内容，包括空轨和句末手工 hit");
            CHECK(generatedRest == seeds && activeRide > 0, "space=1 fill=1 生成成功且仍有 Ride，休止检查非空验证");
            CHECK(restTicks > 0 && restRide == 0, "space=1 fill=1：两个八小节句末的休止区均无 Ride");
            CHECK(restOther == 0, "space=1 fill=1：句末休止区的其他七轨也保持静音");
        }
}

// ===========================================================================
// 0.3.0：三档离散度；统计生成结果，不把名义尝试概率当成最终偏拍比例。
// ===========================================================================
static void runDispersionTests()
{
    section("0.3.0：默认中档与非法档位事务性拒绝");
    CHECK(lattice::Pattern{}.dispersion == 1, "dispersion 默认值为 1（中）");
    for (int tier : {-1, 3, 99})
    {
        auto p = sourceDevelopmentFixture(); p.dispersion = tier;
        const auto before = p;
        CHECK(!p.valid() && !p.generate() && !p.developFromBar1()
              && sourceBarsEqual(p, before), "非法档位拒绝 generate/develop 且不修改 bars");
    }

    constexpr int seeds = 240;
    for (bool backbeat : {false, true})
    {
        std::array<double, 3> rates{}, sourceRates{};
        for (int tier = 0; tier < 3; ++tier)
        {
            long mains = 0, off = 0, sourceMains = 0, sourceOff = 0;
            std::array<long, 2> heads{}, kicks{};
            bool generated = true, deterministic = true, conflictFree = true;
            bool sourceKept = true, sourceGrid = true, silent = true, answerOnly = true;
            bool repeatMotif = true, sourceDev0 = true;
            for (int seed = 0; seed < seeds; ++seed)
            {
                lattice::Pattern p;
                p.seed = seed; p.backbeat = backbeat; p.dispersion = tier;
                generated &= p.generate();
                auto repeat = p; generated &= repeat.generate();
                deterministic &= sourceBarsEqual(p, repeat);
                const auto boundaries = groupBoundaries(p.groups);
                for (int b = 0; b < lattice::phraseBars; ++b)
                {
                    for (int h = 0; h < 2; ++h)
                    {
                        ++heads[h];
                        kicks[h] += velAt(p, b, 0, boundaries[h] * 60) >= 80;
                    }
                    for (int r : {0, 1, 2, 5})
                        for (const auto& c : p.bars[b][r]) if (c.velocity >= 80)
                        { ++mains; off += c.start % lattice::ppq != 0; }
                    for (int t = 0; t < p.barTicks(); t += 60)
                        conflictFree &= !(velAt(p, b, 0, t) >= 80 && velAt(p, b, 1, t) >= 80);
                }
                auto motif = p; motif.development = 0;
                generated &= motif.generate();
                for (int b = 2; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                        repeatMotif &= sourceLaneEqual(motif.bars[b][r], motif.bars[b % 2][r]);

                auto src = sourceDevelopmentFixture();
                src.seed = seed; src.backbeat = backbeat; src.dispersion = tier;
                src.development = .8f; src.fill = src.space = 0;
                // 所有档位使用相同手工源；包含可位移正拍主击和不能重分格的 tuplets。
                for (int r : {0, 1, 5})
                    for (auto& c : src.bars[0][r])
                        if (c.start == (r == 0 ? 480 : r == 1 ? 960 : 1440)) c.velocity = 113 + r;
                const auto before = src;
                const auto answers = sourceAnswerBars(src);
                generated &= src.developFromBar1();
                auto repeatedSource = before; generated &= repeatedSource.developFromBar1();
                deterministic &= sourceBarsEqual(src, repeatedSource);
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                    {
                        const auto& original = before.bars[0][r];
                        const auto& lane = src.bars[b][r];
                        if (b == 0) sourceKept &= sourceLaneEqual(lane, original);
                        sourceGrid &= lane.size() == original.size();
                        if (lane.size() != original.size()) continue;
                        const bool empty = std::all_of(original.begin(), original.end(),
                            [](const auto& c) { return c.velocity == 0; });
                        if (empty) silent &= sourceLaneEqual(lane, original);
                        for (size_t i = 0; i < lane.size(); ++i)
                        {
                            sourceGrid &= lane[i].start == original[i].start && lane[i].length == original[i].length;
                            if (!answers[b]) answerOnly &= lane[i].velocity == original[i].velocity;
                            if (answers[b] && (r == 0 || r == 1 || r == 2 || r == 5) && lane[i].velocity >= 80)
                            { ++sourceMains; sourceOff += lane[i].start % lattice::ppq != 0; }
                        }
                    }
                // 源中的手工强同击允许继承，但不允许出现新的强同击位置。
                for (int b = 1; b < lattice::phraseBars; ++b)
                    for (const auto& c : src.bars[b][0]) if (c.velocity >= 80 && velAt(src, b, 1, c.start) >= 80)
                        conflictFree &= velAt(before, 0, 0, c.start) >= 80 && velAt(before, 0, 1, c.start) >= 80;
                src = before; src.development = 0;
                generated &= src.developFromBar1();
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                        sourceDev0 &= sourceLaneEqual(src.bars[b][r], before.bars[0][r]);
            }
            rates[tier] = double(off) / mains;
            sourceRates[tier] = double(sourceOff) / sourceMains;
            std::printf("        v030 mode=%d tier=%d seeds=%d: head0 kick=%ld/%ld (%.3f%%), "
                        "head1 kick=%ld/%ld (%.3f%%), main off-quarter=%ld/%ld (%.3f%%), "
                        "source answer off-quarter=%ld/%ld (%.3f%%)\n", int(backbeat), tier, seeds,
                        kicks[0], heads[0], 100.0*kicks[0]/heads[0], kicks[1], heads[1], 100.0*kicks[1]/heads[1],
                        off, mains, 100*rates[tier], sourceOff, sourceMains, 100*sourceRates[tier]);
            CHECK(generated && deterministic, "240 seeds 两模式三档：生成/源发展成功且 fixed seed 全 cells 一致");
            CHECK(kicks[0] < heads[0] && kicks[1] < heads[1], "第 0/1 组头 kick 率均不为 100%");
            CHECK(conflictFree, "生成无强 kick/snare 冲突；源发展无新增强同击");
            CHECK(sourceKept && sourceGrid, "源逐 cell 不变，全部发展目标保留非标准 tuplets 起点与长度");
            CHECK(silent, "silent source lanes 在全部目标小节不新增任何音符");
            CHECK(answerOnly, "fill=space=0：非回答小节逐 cell 保留，只有 answer bars 允许主击移动");
            CHECK(repeatMotif && sourceDev0, "dev=0：生成在 motif 层重复；源发展不位移，全部八轨精确复制");
        }
        CHECK(rates[2] > rates[1] && rates[1] > rates[0], "生成主击 off-quarter 聚合比例：高 > 中 > 低");
        CHECK(sourceRates[2] > sourceRates[1] && sourceRates[1] > sourceRates[0],
              "固定手工源 answer 主击 off-quarter 聚合比例：高 > 中 > 低");
    }

    section("0.3.0：helper 共享固定源，名义移动率及严格守恒");
    for (int denominator : {4, 8})
    {
        lattice::Pattern fixture; fixture.meter(8, denominator);
        const int beat = denominator == 8 ? 240 : 480;
        for (int r : {0, 1, 2, 5})
            for (auto& c : fixture.bars[0][r])
                if (c.start % beat == 0 && (r > 1 || (c.start / beat) % 2 == r)) c.velocity = 101 + r;
        fixture.bars[0][3][1].velocity = 40;
        fixture.bars[0][7][0].velocity = 127;
        std::array<long, 3> moved{}, candidates{};
        bool nested = true, conserved = true, deterministic = true, noClash = true;
        for (int seed = 0; seed < seeds; ++seed)
        {
            std::array<std::set<std::pair<int, int>>, 3> moves;
            for (int tier = 0; tier < 3; ++tier)
            {
                fixture.dispersion = tier;
                auto bar = fixture.bars[0], repeat = bar;
                fixture.syncopateBar(bar, unsigned(seed));
                fixture.syncopateBar(repeat, unsigned(seed));
                for (int r = 0; r < lattice::tracks; ++r)
                {
                    deterministic &= sourceLaneEqual(bar[r], repeat[r]);
                    const auto& original = fixture.bars[0][r];
                    conserved &= bar[r].size() == original.size();
                    std::multiset<int> beforeHits, afterHits;
                    for (size_t i = 0; i < original.size(); ++i)
                    {
                        conserved &= bar[r][i].start == original[i].start && bar[r][i].length == original[i].length;
                        if (original[i].velocity) beforeHits.insert(original[i].velocity);
                        if (bar[r][i].velocity) afterHits.insert(bar[r][i].velocity);
                        if (r == 0 || r == 1 || r == 2 || r == 5)
                        {
                            if (original[i].velocity >= 80)
                            {
                                ++candidates[tier];
                                if (bar[r][i].velocity == 0) { ++moved[tier]; moves[tier].insert({r, original[i].start}); }
                            }
                        }
                        else conserved &= bar[r][i].velocity == original[i].velocity;
                    }
                    conserved &= beforeHits == afterHits;
                }
                for (size_t i = 0; i < bar[0].size(); ++i)
                    noClash &= !(bar[0][i].velocity >= 80 && bar[1][i].velocity >= 80);
            }
            nested &= std::includes(moves[1].begin(), moves[1].end(), moves[0].begin(), moves[0].end())
                && std::includes(moves[2].begin(), moves[2].end(), moves[1].begin(), moves[1].end());
        }
        for (int tier = 0; tier < 3; ++tier)
        {
            const double rate = double(moved[tier]) / candidates[tier];
            const double nominal[] = {.10, .35, .65};
            std::printf("        v030 helper denominator=%d tier=%d: moved=%ld/%ld (%.3f%%), nominal=%.0f%%\n",
                        denominator, tier, moved[tier], candidates[tier], 100*rate, 100*nominal[tier]);
            CHECK(std::abs(rate - nominal[tier]) < .035, "无碰撞固定源 helper 移动率接近标称（绝对误差 < 3.5 百分点）");
        }
        CHECK(nested && conserved && deterministic && noClash,
              "helper 三档共享候选：低包含于中包含于高，力度/数量/cells 守恒，镲不动，无强冲突");
    }

    section("0.3.0：全部锁轨/锁小节组合与全部拍号、档位、模式");
    bool locks = true, safe = true, palette = true, calls = true, tuplet = true;
    for (int tier = 0; tier < 3; ++tier)
        for (bool backbeat : {false, true})
            for (bool develop : {false, true})
            {
                for (int track = 0; track < lattice::tracks; ++track)
                    for (int bar = 0; bar < lattice::phraseBars; ++bar)
                    {
                        auto p = sourceDevelopmentFixture();
                        p.dispersion = tier; p.backbeat = backbeat; p.seed = 31 + bar * 8 + track;
                        p.locked[track] = true; p.barLocked[bar] = true;
                        const auto before = p;
                        calls &= develop ? p.developFromBar1() : p.generate();
                        for (int b = 0; b < lattice::phraseBars; ++b)
                            for (int r = 0; r < lattice::tracks; ++r)
                                if (b == bar || r == track || (develop && b == 0))
                                    locks &= sourceLaneEqual(p.bars[b][r], before.bars[b][r]);
                    }
                for (int n = 1; n <= 16; ++n)
                    for (int d : {4, 8})
                        for (int seed : {0, 7, 1729})
                        {
                            auto p = sourceDevelopmentFixture(n, d);
                            p.dispersion = tier; p.backbeat = backbeat; p.seed = seed;
                            p.ghost = p.accents = false; p.fill = 1; p.development = .8f;
                            // 关闭开关不应改写手工力度：这里用无 40/127 的源检查不新增它们。
                            for (auto& lane : p.bars[0]) for (auto& c : lane) if (c.velocity) c.velocity = 80;
                            const auto before = p;
                            calls &= develop ? p.developFromBar1() : p.generate();
                            for (int b = 0; b < lattice::phraseBars; ++b)
                                for (int r = 0; r < lattice::tracks; ++r)
                                {
                                    int end = 0;
                                    for (const auto& c : p.bars[b][r])
                                    {
                                        safe &= c.start == end && c.length > 0 && c.length <= p.barTicks() - end;
                                        safe &= c.velocity >= 0 && c.velocity <= 127;
                                        palette &= c.velocity == 0 || c.velocity == 80;
                                        end += c.length;
                                    }
                                    safe &= end == p.barTicks();
                                    if (develop)
                                    {
                                        const auto& a = p.bars[b][r]; const auto& z = before.bars[0][r];
                                        tuplet &= a.size() == z.size();
                                        if (a.size() == z.size()) for (size_t i = 0; i < a.size(); ++i)
                                            tuplet &= a[i].start == z[i].start && a[i].length == z[i].length;
                                    }
                                }
                        }
            }
    CHECK(calls && locks, "1536 个组合：两种操作、两模式、三档、8 轨 x 16 bar 锁定内容逐 cell 不变");
    CHECK(safe && tuplet, "1152 个拍号用例：1..16、分母 4/8、两操作两模式三档均连续、不越界、不破坏 tuplets");
    CHECK(palette, "ghost/accents 关闭：生成与无特殊力度手工源发展均不新增 40/127");

    bool flags = true, manualKept = true, lockedConflicts = true;
    for (int tier = 0; tier < 3; ++tier)
        for (bool backbeat : {false, true})
            for (int seed = 0; seed < seeds; ++seed)
            {
                for (bool ghost : {false, true})
                    for (bool accents : {false, true})
                    {
                        lattice::Pattern p;
                        p.dispersion = tier; p.backbeat = backbeat; p.seed = seed;
                        p.ghost = ghost; p.accents = accents;
                        calls &= p.generate();
                        for (const auto& bar : p.bars) for (const auto& lane : bar) for (const auto& c : lane)
                            flags &= (ghost || c.velocity != 40) && (accents || c.velocity != 127);
                    }
                auto source = sourceDevelopmentFixture();
                source.dispersion = tier; source.backbeat = backbeat; source.seed = seed;
                source.ghost = source.accents = false; source.development = 0;
                source.bars[0][0][0].velocity = 127; source.bars[0][1][1].velocity = 40;
                const auto original = source;
                calls &= source.developFromBar1();
                for (int b = 0; b < lattice::phraseBars; ++b)
                    for (int r = 0; r < lattice::tracks; ++r)
                        manualKept &= sourceLaneEqual(source.bars[b][r], original.bars[0][r]);
                for (int locked : {0, 1})
                    for (bool develop : {false, true})
                    {
                        auto p = original; p.development = .8f; p.locked[locked] = true;
                        for (int b = 1; b < lattice::phraseBars; ++b)
                        {
                            // 使实际锁轨与源不同，并覆盖所有候选落点。
                            p.bars[b][locked].clear();
                            for (int t = 0; t < p.barTicks(); t += 60)
                                p.bars[b][locked].push_back({t, 60, t % 120 == 0 ? 103 : 0});
                        }
                        const auto before = p;
                        calls &= develop ? p.developFromBar1() : p.generate();
                        for (int b = 1; b < lattice::phraseBars; ++b)
                        {
                            lockedConflicts &= sourceLaneEqual(p.bars[b][locked], before.bars[b][locked]);
                            for (const auto& c : p.bars[b][locked]) if (c.velocity >= 80)
                                lockedConflicts &= velAt(p, b, 1 - locked, c.start) < 80;
                        }
                    }
            }
    CHECK(calls && flags, "240 seeds 两模式三档四种开关组合：ghost off 无 40，accents off 无 127");
    CHECK(manualKept, "dev=0 不因开关关闭而擦除源手工 40/127，精确复制优先");
    CHECK(lockedConflicts, "两种操作三档：锁定 kick/snare 的真实目标落点被避让，整轨仍精确保留");

    bool helperTuplets = true;
    for (int tier = 0; tier < 3; ++tier)
        for (int seed = 0; seed < seeds; ++seed)
        {
            auto p = sourceDevelopmentFixture(); p.dispersion = tier;
            const auto original = p.bars[0]; auto bar = original;
            p.syncopateBar(bar, unsigned(seed));
            for (int r = 0; r < lattice::tracks; ++r)
            {
                helperTuplets &= bar[r].size() == original[r].size();
                if (bar[r].size() != original[r].size()) continue;
                for (size_t i = 0; i < bar[r].size(); ++i)
                {
                    helperTuplets &= bar[r][i].start == original[r][i].start && bar[r][i].length == original[r][i].length;
                    if (original[r][i].start < 120) helperTuplets &= bar[r][i].velocity == original[r][i].velocity;
                }
            }
        }
    CHECK(helperTuplets, "helper 不把 40tick tuplet 主击搬入 60tick cell，也不量化或删除分格");
}

// ===========================================================================
// MIDI EXPORT TESTS (only when Processor.h is available)
// ===========================================================================
#if __has_include("Processor.h")
#include "Processor.h"
#include "GroupEditorTests.h"
#include "WavExportTests.h"

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
        CHECK(allGm, "all 8 GM pitches (36,38,41,42,46,45,51,49) present");
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

    section("Processor: dispersion 三档 roundtrip、缺失默认中档、非法属性整份拒绝");
    for (int tier = 0; tier < 3; ++tier)
    {
        LatticeProcessor proc; proc.pattern.dispersion = tier;
        proc.pattern.seed = 918 + tier; proc.pattern.backbeat = tier != 0;
        proc.pattern.generate(); proc.pattern.barLocked[3] = true; proc.pattern.locked[5] = true;
        proc.pattern.subdivide(0, 2, 0, 3, 3);
        juce::MemoryBlock state; proc.getStateInformation(state);
        LatticeProcessor copy; copy.pattern.dispersion = (tier + 1) % 3;
        copy.setStateInformation(state.getData(), int(state.getSize()));
        juce::MemoryBlock restored; copy.getStateInformation(restored);
        CHECK(copy.pattern.dispersion == tier && state == restored && sourceBarsEqual(proc.pattern, copy.pattern),
              "三档 state 全字节 roundtrip，包括参数、锁与 tuplets，不重新生成 bars");
        auto xml = juce::AudioProcessor::getXmlFromBinary(state.getData(), int(state.getSize()));
        CHECK(xml != nullptr, "三档 state 可解析为 XML");
        if (!xml) continue;
        xml->removeAttribute("dispersion");
        juce::MemoryBlock legacy; juce::AudioProcessor::copyXmlToBinary(*xml, legacy);
        copy.pattern.dispersion = 2;
        copy.setStateInformation(legacy.getData(), int(legacy.getSize()));
        CHECK(copy.pattern.dispersion == 1 && sourceBarsEqual(copy.pattern, proc.pattern),
              "v2 缺失 dispersion 恢复默认 1，不沿用接收者旧值且不改源 bars");
        for (const char* invalid : {"-1", "3", "99", "", "x", "1.0", "01", " 1", "1 ", "1x", "nan"})
        {
            xml->setAttribute("dispersion", invalid);
            juce::MemoryBlock bad; juce::AudioProcessor::copyXmlToBinary(*xml, bad);
            LatticeProcessor target; target.pattern.meter(7, 8); target.pattern.groups = {3, 2, 2};
            target.pattern.seed = 444; target.pattern.dispersion = 0; target.pattern.generate();
            target.pattern.locked[0] = true; target.pattern.barLocked[15] = true;
            juce::MemoryBlock before, after; target.getStateInformation(before);
            target.setStateInformation(bad.getData(), int(bad.getSize()));
            target.getStateInformation(after);
            CHECK(before == after, "非法 dispersion 拒绝整个 state，参数/拍号/锁/bars 全字节不变");
        }
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
        LatticeProcessor proc; proc.pattern.dispersion = 2;
        proc.setStateInformation(mb.getData(), (int)mb.getSize());
        const auto& m = proc.pattern;
        CHECK(m.dispersion == 1, "v1 缺失 dispersion 迁移为默认中档 1");
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

// ===========================================================================
// EDITOR CONTROL INTROSPECTION (no VST3 host, no DocumentWindow)
// Construct the LatticeProcessor editor directly via createEditor(), then
// recursively walk the live component tree and READ actual control state:
//   * tempo 与五个真实垂直 fader，亮色文字配深色底及参数回调映射；
//   * 隐藏兼容三档 TextButton 的映射、互斥、状态隔离及连续值只读同步；
//   * the 16 bar TextButtons driven by playTick -> toggle the "9" button when
//     playTick points at the 9th bar, and KEEP it lit after playTick = -1.
// Reuses the same createEditor() + dispatch-loop pattern as runGuiTest, but
// introspects controls instead of screenshotting.
// ===========================================================================
static void runEditorControlsTest()
{
    g_usedProcessor = true;
    section("Editor: createEditor + recursive control introspection (no host)");

    LatticeProcessor p;
    {
        juce::ScopedLock lk(p.modelLock);
        p.pattern.generate();                 // valid 4/4 -> barTicks() == 1920
    }

    // Direct editor construction. Per JUCE convention createEditor() returns a
    // newly-allocated editor the caller owns and must delete; we hold it in a
    // unique_ptr so it is destroyed cleanly when this test scope exits.
    std::unique_ptr<juce::AudioProcessorEditor> editor(p.createEditor());
    CHECK(editor != nullptr, "editor created via LatticeProcessor::createEditor()");
    if (editor == nullptr) return;
    editor->setVisible(true);

    // --- Recursive walk: collect every Slider and Label in the live tree. -----
    juce::Array<juce::Slider*> sliders;
    juce::Array<juce::Label*>  labels;
    std::function<void(juce::Component*)> walk = [&](juce::Component* c)
    {
        if (c == nullptr) return;
        if (auto* s = dynamic_cast<juce::Slider*>(c)) sliders.add(s);
        if (auto* l = dynamic_cast<juce::Label*>(c))  labels.add(l);
        for (int i = 0; i < c->getNumChildComponents(); ++i)
            walk(c->getChildComponent(i));
    };
    walk(editor.get());

    CHECK(sliders.size() == 6,
          "包含 tempo Slider 及五个垂直参数 Faders");
    juce::Slider* tempoSlider = nullptr;
    int tempoCount = 0;
    std::vector<juce::Slider*> faders;
    for (auto* s : sliders)
    {
        if (s->getSliderStyle() == juce::Slider::LinearHorizontal
            && s->getMinimum() == 40 && s->getMaximum() == 240)
        {
            tempoSlider = s;
            ++tempoCount;
        }
        if (s->getSliderStyle() == juce::Slider::LinearVertical) faders.push_back(s);
    }
    CHECK(tempoCount == 1, "唯一 BPM Slider 为 LinearHorizontal，范围 40–240");
    CHECK(faders.size() == 5, "恰好五个真实 LinearVertical 参数 fader");
    std::sort(faders.begin(), faders.end(), [](const auto* a, const auto* b) { return a->getX() < b->getX(); });
    std::printf("        (found %d Slider(s), %d Label(s) in editor tree)\n",
                sliders.size(), labels.size());

    // 读取实际控件配色，并确认 tempo 的内部 Label 确实使用亮字深底。
    if (tempoSlider != nullptr)
    {
        CHECK(tempoSlider->findColour(juce::Slider::textBoxTextColourId).getBrightness() > 0.7f
              && tempoSlider->findColour(juce::Slider::textBoxBackgroundColourId).getBrightness() < 0.3f,
              "tempo Slider 文本框使用亮色文字与深色底");
        int tempoLabels = 0;
        for (auto* label : labels)
            if (tempoSlider->isParentOf(label))
            {
                ++tempoLabels;
                CHECK(label->findColour(juce::Label::textColourId).getBrightness() > 0.7f
                      && label->findColour(juce::Label::backgroundColourId).getBrightness() < 0.3f,
                      "tempo 内部 Label 使用亮色文字与深色底");
            }
        CHECK(tempoLabels > 0, "递归找到 tempo 的实际文本框 Label");
    }

    // --- playTick drives the bar TextButtons. ---------------------------------
    auto findBarButton = [&](const juce::String& name) -> juce::TextButton*
    {
        juce::TextButton* found = nullptr;
        std::function<void(juce::Component*)> f = [&](juce::Component* c)
        {
            if (c == nullptr || found != nullptr) return;
            if (auto* tb = dynamic_cast<juce::TextButton*>(c))
                if (tb->isVisible() && tb->getButtonText() == name) { found = tb; return; }
            for (int i = 0; i < c->getNumChildComponents() && found == nullptr; ++i)
                f(c->getChildComponent(i));
        };
        f(editor.get());
        return found;
    };

    const int targetBar = 8;   // 0-indexed bar for the 9th bar -> TextButton "9"
    p.playTick.store(targetBar * p.pattern.barTicks());   // -> bar 9

    // Let the editor's 25 Hz timer run; it reads playTick and re-syncs buttons.
    juce::Thread::sleep(100);juce::Timer::callPendingTimersSynchronously();

    juce::TextButton* b9 = findBarButton("9");
    CHECK(b9 != nullptr, "TextButton named '9' found in editor tree");
    CHECK(b9 != nullptr && b9->getToggleState(),
          "TextButton '9' is toggled after playTick -> bar 9");

    // Stop: playTick = -1 keeps the last bar selected (button 9 stays lit).
    p.playTick.store(-1);
    juce::Thread::sleep(100);juce::Timer::callPendingTimersSynchronously();

    b9 = findBarButton("9");
    CHECK(b9 != nullptr && b9->getToggleState(),
          "TextButton '9' remains toggled after playTick = -1 (bar retained)");

    // 新回归独立保存状态，避免随机 seed、锁与鼠标编辑污染原有源发展验收。
    const auto controlsFixture = p.pattern;
    auto flushEditorTimers = []
    {
        juce::Thread::sleep(100);
        juce::Timer::callPendingTimersSynchronously();
    };
    auto checkVisibleControls = [&]
    {
        CHECK(editor->getWidth() == 1120 && editor->getHeight() == 760,
              "编辑器保持 1120x760 尺寸");
        std::vector<juce::Component*> controls;
        for (int i = 0; i < editor->getNumChildComponents(); ++i)
        {
            auto* c = editor->getChildComponent(i);
            if (c->isVisible() && (dynamic_cast<juce::Button*>(c)
                || dynamic_cast<juce::Slider*>(c) || dynamic_cast<juce::TextEditor*>(c)
                || dynamic_cast<juce::ComboBox*>(c)))
                controls.push_back(c);
        }
        CHECK(!controls.empty(), "布局检查找到可见的直接子控件，不计内部 Label 或其他子孙");
        bool contained = true, disjoint = true;
        for (size_t i = 0; i < controls.size(); ++i)
        {
            const auto bounds = controls[i]->getBounds();
            contained &= !bounds.isEmpty() && editor->getLocalBounds().contains(bounds);
            for (size_t j = i + 1; j < controls.size(); ++j)
                disjoint &= !bounds.intersects(controls[j]->getBounds());
        }
        CHECK(contained, "所有可见直接子控件具有正尺寸且完整位于 1120x760 内");
        CHECK(disjoint, "所有可见直接子 Button/Slider/TextEditor/ComboBox 两两无重叠");
    };
    checkVisibleControls();
    section("Editor：五个可见 fader 与隐藏兼容三档按钮的真实回调、只读同步");
    struct TierSpec
    {
        const char* name;
        float lattice::Pattern::* member;
        std::array<float, 3> values;
    };
    const std::array<TierSpec, 5> specs{{
        {"development", &lattice::Pattern::development, {0.f, .45f, .85f}},
        {"fill", &lattice::Pattern::fill, {0.f, .5f, 1.f}},
        {"space", &lattice::Pattern::space, {0.f, .35f, .7f}},
        {"density", &lattice::Pattern::density, {.25f, .55f, .85f}},
        {"syncopation", nullptr, {0.f, 1.f, 2.f}}
    }};
    if (faders.size() == specs.size())
        for (size_t group = 0; group < specs.size(); ++group)
        {
            auto* fader = faders[group];
            const auto& spec = specs[group];
            const double maximum = spec.member != nullptr ? 1.0 : 2.0;
            const auto message = juce::String(spec.name) + "：可见可用、无文本框、范围及真实回调正确";
            CHECK(fader->isVisible() && fader->isEnabled() && bool(fader->onValueChange)
                  && fader->getTextBoxPosition() == juce::Slider::NoTextBox
                  && fader->getMinimum() == 0.0 && fader->getMaximum() == maximum
                  && std::abs(fader->getInterval() - 0.01) < 1e-9, message.toRawUTF8());
            // 同步通知触发真实 onValueChange；覆盖端点、连续值及 syncopation 四舍五入。
            for (double input : {0.0, maximum * 0.37, maximum * 0.76, maximum})
            {
                auto expected = p.pattern;
                const double target = spec.member != nullptr ? input : std::round(input);
                if (spec.member != nullptr) expected.*spec.member = float(target);
                else expected.dispersion = int(target);
                LatticeProcessor reference;
                reference.pattern = expected;
                juce::MemoryBlock expectedState, actualState;
                reference.getStateInformation(expectedState);
                fader->setValue(input, juce::sendNotificationSync);
                p.getStateInformation(actualState);
                const double actual = spec.member != nullptr ? double(p.pattern.*spec.member)
                                                            : double(p.pattern.dispersion);
                const auto mappingMessage = juce::String(spec.name) + "：fader 回调正确映射 "
                    + juce::String(input, 2) + "，仅修改目标参数且同步显示实际值";
                CHECK(std::abs(actual - target) < 1e-6 && std::abs(fader->getValue() - target) < 1e-6
                      && actualState == expectedState, mappingMessage.toRawUTF8());
            }
        }
    {
        juce::ScopedLock lk(p.modelLock);
        p.pattern = controlsFixture;
    }
    flushEditorTimers();
    const std::array<const char*, 3> suffixes{{"-low", "-mid", "-high"}};
    const std::array<const char*, 3> captions{{"LOW", "MID", "HIGH"}};
    using TierButtons = std::array<std::array<juce::TextButton*, 3>, 5>;
    auto collectTiers = [&](juce::Component& root)
    {
        TierButtons buttons{};
        std::array<std::array<int, 3>, 5> counts{};
        int captionCount = 0;
        std::function<void(juce::Component&)> visit = [&](juce::Component& c)
        {
            if (auto* button = dynamic_cast<juce::TextButton*>(&c))
            {
                for (const auto* caption : captions)
                    captionCount += button->getButtonText() == caption;
                for (size_t group = 0; group < specs.size(); ++group)
                    for (size_t tier = 0; tier < suffixes.size(); ++tier)
                        if (button->getName() == juce::String(specs[group].name) + suffixes[tier])
                        {
                            buttons[group][tier] = button;
                            ++counts[group][tier];
                        }
            }
            for (int i = 0; i < c.getNumChildComponents(); ++i)
                visit(*c.getChildComponent(i));
        };
        visit(root);
        CHECK(captionCount == 15, "恰好十五个 LOW/MID/HIGH TextButton，无重复或遗留档位按钮");
        for (size_t group = 0; group < specs.size(); ++group)
            for (size_t tier = 0; tier < suffixes.size(); ++tier)
            {
                const auto* button = buttons[group][tier];
                const auto message = juce::String(specs[group].name) + suffixes[tier]
                    + "：name 唯一、文案正确、隐藏兼容按钮可用且绑定真实 onClick";
                CHECK(counts[group][tier] == 1 && button != nullptr
                      && button->getButtonText() == captions[tier] && !button->isVisible()
                      && button->isEnabled() && bool(button->onClick), message.toRawUTF8());
            }
        return buttons;
    };
    auto checkTierDisplay = [&](const TierButtons& buttons, const lattice::Pattern& expected)
    {
        for (size_t group = 0; group < specs.size(); ++group)
        {
            const auto& spec = specs[group];
            const double current = spec.member != nullptr ? double(expected.*spec.member)
                                                          : double(expected.dispersion);
            int nearest = 0, selected = 0;
            for (int tier = 1; tier < 3; ++tier)
                if (std::abs(current - spec.values[tier]) < std::abs(current - spec.values[nearest]))
                    nearest = tier;
            bool tooltipOK = true;
            for (int tier = 0; tier < 3; ++tier)
            {
                auto* button = buttons[group][tier];
                selected += button != nullptr && button->getToggleState();
                if (spec.member != nullptr)
                    tooltipOK &= button != nullptr && button->getTooltip().contains(
                        "Current value: " + juce::String(current, 6) + ".")
                        && button->getTooltip().contains(juce::String(captions[tier])
                            + " target: " + juce::String(double(spec.values[tier]), 2) + ".");
            }
            const auto selectionMessage = juce::String(spec.name) + "：恰好一档选中且为最近档";
            CHECK(selected == 1 && buttons[group][nearest] != nullptr
                  && buttons[group][nearest]->getToggleState(), selectionMessage.toRawUTF8());
            // Syncopation 的真实同步只设置 toggle，不提供 tooltip。
            if (spec.member != nullptr)
            {
                const auto tooltipMessage = juce::String(spec.name) + "：三按钮 tooltip 显示实际值及各自目标值";
                CHECK(tooltipOK, tooltipMessage.toRawUTF8());
            }
        }
    };
    const auto tierButtons = collectTiers(*editor);
    checkTierDisplay(tierButtons, p.pattern);
    CHECK(p.pattern.dispersion == 1 && tierButtons[4][1] != nullptr
          && tierButtons[4][1]->getToggleState(), "Syncopation 默认 MID，对应 dispersion=1");

    auto tierFixture = sourceDevelopmentFixture();
    tierFixture.groups = {3, 3, 2};
    tierFixture.seed = 24681357;
    tierFixture.bpm = 137;
    tierFixture.gridStep = 60;
    tierFixture.ghost = false;
    tierFixture.accents = false;
    tierFixture.backbeat = true;
    tierFixture.locked[1] = tierFixture.locked[6] = true;
    tierFixture.barLocked[0] = tierFixture.barLocked[7] = tierFixture.barLocked[15] = true;
    // state 加载仅接受 0/40/80/127；保留 fixture 的 tuplets 与各小节差异。
    for (auto& bar : tierFixture.bars)
        for (auto& lane : bar)
            for (auto& cell : lane)
                if (cell.velocity > 0) cell.velocity = cell.velocity >= 100 ? 127 : cell.velocity >= 60 ? 80 : 40;
    {
        juce::ScopedLock lk(p.modelLock);
        p.pattern = tierFixture;
        p.changed();
    }
    flushEditorTimers();
    int mappingsTested = 0;
    for (size_t group = 0; group < specs.size(); ++group)
        for (int tier : {0, 2, 1})
        {
            auto* button = tierButtons[group][tier];
            if (button == nullptr || !button->onClick) continue;
            const auto before = p.pattern;
            auto expected = before;
            const auto& spec = specs[group];
            if (spec.member != nullptr) expected.*spec.member = spec.values[tier];
            else expected.dispersion = tier;
            LatticeProcessor reference;
            reference.pattern = expected;
            juce::MemoryBlock expectedState;
            reference.getStateInformation(expectedState);
            std::printf("        点击 %s%s -> %.2f\n", spec.name, suffixes[tier], double(spec.values[tier]));
            for (int repeat = 0; repeat < 2; ++repeat)
            {
                button->onClick(); // 不手动设置 toggle，也不模拟写入目标参数。
                juce::MemoryBlock actualState;
                p.getStateInformation(actualState);
                CHECK((spec.member != nullptr ? p.pattern.*spec.member == spec.values[tier]
                                              : p.pattern.dispersion == tier),
                      "真实 onClick 精确写入对应档位值，重复点击不会取消选中");
                CHECK(actualState == expectedState && sourceBarsEqual(p.pattern, before)
                      && p.pattern.seed == before.seed && p.pattern.locked == before.locked
                      && p.pattern.barLocked == before.barLocked,
                      "真实 onClick 仅改变目标参数，其他完整 state、bars、seed、两类锁不变");
                checkTierDisplay(tierButtons, expected);
            }
            flushEditorTimers();
            juce::MemoryBlock afterTimer;
            p.getStateInformation(afterTimer);
            CHECK(afterTimer == expectedState && sourceBarsEqual(p.pattern, before),
                  "切档后 timer 不回退参数或异步生成，完整 state 不变");
            checkTierDisplay(tierButtons, expected);
            ++mappingsTested;
        }
    CHECK(mappingsTested == 15, "五组三档全部十五个映射均执行真实 onClick");

    section("Editor 0.3.1：旧连续值加载、构造 sync 与多轮 timer 不量化");
    LatticeProcessor legacy;
    legacy.pattern = tierFixture;
    legacy.pattern.density = .43f;
    legacy.pattern.development = .31f;
    legacy.pattern.fill = .62f;
    legacy.pattern.space = .19f;
    legacy.pattern.dispersion = 2;
    juce::MemoryBlock legacyState;
    legacy.getStateInformation(legacyState);
    auto checkLegacyState = [&](LatticeProcessor& target)
    {
        juce::MemoryBlock actual;
        target.getStateInformation(actual);
        CHECK(target.pattern.density == .43f && target.pattern.development == .31f
              && target.pattern.fill == .62f && target.pattern.space == .19f,
              "旧 float 精确保持 density=.43 development=.31 fill=.62 space=.19，未被最近档覆盖");
        CHECK(actual == legacyState && sourceBarsEqual(target.pattern, legacy.pattern),
              "加载与显示同步保留原 state 全字节，包括 bars、seed 与锁");
    };
    {
        LatticeProcessor loaded;
        loaded.setStateInformation(legacyState.getData(), int(legacyState.getSize()));
        checkLegacyState(loaded);
        std::unique_ptr<juce::AudioProcessorEditor> loadedEditor(loaded.createEditor());
        CHECK(loadedEditor != nullptr, "旧 state 加载后可创建编辑器，执行构造 sync");
        if (loadedEditor != nullptr)
        {
            loadedEditor->setVisible(true);
            const auto loadedButtons = collectTiers(*loadedEditor);
            checkTierDisplay(loadedButtons, legacy.pattern);
            checkLegacyState(loaded);
            for (int tick = 0; tick < 3; ++tick)
            {
                flushEditorTimers();
                checkTierDisplay(loadedButtons, legacy.pattern);
                checkLegacyState(loaded);
            }
        }
    }
    // 从全 LOW 显示加载旧工程，确保 timer 真正刷新而非沿用已有 MID 高亮。
    for (auto& group : tierButtons)
        if (group[0] != nullptr && group[0]->onClick) group[0]->onClick();
    checkTierDisplay(tierButtons, p.pattern);
    p.setStateInformation(legacyState.getData(), int(legacyState.getSize()));
    checkLegacyState(p);
    for (int tick = 0; tick < 3; ++tick)
    {
        flushEditorTimers();
        checkTierDisplay(tierButtons, legacy.pattern);
        checkLegacyState(p);
    }
    {
        juce::ScopedLock lk(p.modelLock);
        p.pattern = controlsFixture;
        p.changed();
    }
    flushEditorTimers();
    checkVisibleControls();
    auto* generateButton = findBarButton("GENERATE");
    auto* sourceButton = findBarButton("DEVELOP BAR 1");
    CHECK(generateButton != nullptr && sourceButton != nullptr, "两个主操作按钮存在");
    if (generateButton != nullptr && sourceButton != nullptr)
    {
        const auto a = generateButton->getBounds(), b = sourceButton->getBounds();
        CHECK(a.getX() == b.getX() && a.getWidth() == b.getWidth()
              && a.getX() >= editor->getWidth() / 2,
              "GENERATE 与 DEVELOP BAR 1 位于右侧、同 x 且等宽");
        CHECK(a.getHeight() == 56 && b.getHeight() == 50
              && a.getBottom() < b.getY() && !a.intersects(b),
              "两个主按钮上下排列且不相交，高度分别为 56/50");
    }

    std::function<juce::Component*(juce::Component*, const juce::String&)> findByID =
        [&](juce::Component* root, const juce::String& id) -> juce::Component*
    {
        if (root->getComponentID() == id) return root;
        for (int i = 0; i < root->getNumChildComponents(); ++i)
            if (auto* found = findByID(root->getChildComponent(i), id)) return found;
        return nullptr;
    };
    auto modelState = [&]
    {
        juce::MemoryBlock state;
        p.getStateInformation(state);
        return state;
    };
    auto* groupDraft = findByID(editor.get(), "group-editor");
    auto* groupInput = dynamic_cast<juce::TextEditor*>(findByID(editor.get(), "group-draft"));
    auto* groupCancel = dynamic_cast<juce::TextButton*>(findByID(editor.get(), "group-cancel"));
    CHECK(groupDraft != nullptr && !groupDraft->isVisible() && groupInput != nullptr
          && groupCancel != nullptr && bool(groupCancel->onClick),
          "按 componentID 找到初始隐藏的分组浮层、草稿输入和真实取消回调");

    juce::ToggleButton* followButton = nullptr;
    juce::ComboBox* meterControl = nullptr;
    juce::TextEditor* seedInput = nullptr;
    int seedInputs = 0;
    for (int i = 0; i < editor->getNumChildComponents(); ++i)
    {
        auto* child = editor->getChildComponent(i);
        if (auto* toggle = dynamic_cast<juce::ToggleButton*>(child))
            if (toggle->getButtonText() == "FOLLOW") followButton = toggle;
        if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
            if (combo->getNumItems() == 4) meterControl = combo;
        // seed 尚无专用 ID；只选默认拍号下可见的直接子文本框，显式排除草稿。
        if (auto* input = dynamic_cast<juce::TextEditor*>(child))
            if (input->getComponentID() != "group-draft" && input->isVisible())
            {
                seedInput = input;
                ++seedInputs;
            }
    }
    CHECK(seedInputs == 1 && seedInput != nullptr && bool(seedInput->onReturnKey)
          && seedInput->getText() == juce::String(p.pattern.seed),
          "唯一可见非 group-draft 直接子文本框为 seed，显示模型值并绑定提交回调");
    auto* b1 = findBarButton("1");
    auto* b3 = findBarButton("3");
    auto* b16 = findBarButton("16");
    CHECK(followButton != nullptr && bool(followButton->onClick), "FOLLOW 存在且绑定真实回调");
    CHECK(followButton != nullptr && followButton->getToggleState(), "FOLLOW 默认开启");
    CHECK(b1 != nullptr && bool(b1->onClick) && b3 != nullptr && bool(b3->onClick)
          && b16 != nullptr && bool(b16->onClick), "第 1、3、16 小节选择按钮及回调存在");
    if (followButton != nullptr && followButton->onClick && b3 != nullptr && b3->onClick)
    {
        followButton->setToggleState(false, juce::dontSendNotification);
        followButton->onClick();
        p.playTick.store(8 * p.pattern.barTicks());
        b3->onClick();
        for (int i = 0; i < 3; ++i) flushEditorTimers();
        CHECK(!followButton->getToggleState() && b3->getToggleState()
              && b9 != nullptr && !b9->getToggleState(),
              "FOLLOW off：playTick 在 bar9，手选 bar3 后多轮 timer 仍保持 bar3");
        followButton->setToggleState(true, juce::dontSendNotification);
        followButton->onClick();
        flushEditorTimers();
        CHECK(followButton->getToggleState() && b9 != nullptr && b9->getToggleState()
              && !b3->getToggleState(), "FOLLOW 重新开启后自动跟随至 bar9");
    }
    p.playTick.store(-1);

    auto* seedButton = findBarButton("REROLL");
    CHECK(seedButton != nullptr && bool(seedButton->onClick), "REROLL 存在且绑定真实回调");
    if (seedButton != nullptr && seedButton->onClick)
    {
        const auto before = p.pattern;
        auto settingsUnchanged = [&]
        {
            const auto& m = p.pattern;
            return m.numerator == before.numerator && m.denominator == before.denominator
                && m.bpm == before.bpm && m.density == before.density
                && m.ghost == before.ghost && m.accents == before.accents
                && m.backbeat == before.backbeat && m.development == before.development
                && m.fill == before.fill && m.space == before.space && m.dispersion == before.dispersion
                && m.gridStep == before.gridStep && m.groups == before.groups
                && m.locked == before.locked && m.barLocked == before.barLocked;
        };
        bool seedChanged = false;
        for (int attempt = 0; attempt < 8; ++attempt)
        {
            seedButton->onClick();
            seedChanged |= p.pattern.seed != before.seed;
            CHECK(sourceBarsEqual(p.pattern, before), "REROLL 回调立即保留全部 bars 的起点、长度与力度");
            CHECK(settingsUnchanged(), "REROLL 不改变 seed 以外的模型设置或锁");
            const int chosenSeed = p.pattern.seed;
            for (int i = 0; i < 3; ++i) flushEditorTimers();
            CHECK(sourceBarsEqual(p.pattern, before), "REROLL 等待多轮 pending timers 后仍未异步 generate");
            CHECK(p.pattern.seed == chosenSeed && settingsUnchanged(), "pending timers 不回退 seed 或改变其他设置");
        }
        CHECK(seedChanged, "REROLL 连续八次至少一次改变 seed，不要求每次随机值都不同");
    }

    auto* variateButton = findBarButton("VARIATE");
    auto* copyButton = findBarButton("COPY >");
    CHECK(variateButton != nullptr && copyButton != nullptr, "VARIATE 与 COPY 按钮存在");
    if (b3 != nullptr && b3->onClick && b16 != nullptr && b16->onClick
        && variateButton != nullptr && copyButton != nullptr)
    {
        {
            juce::ScopedLock lk(p.modelLock);
            p.pattern.barLocked.fill(false);
        }
        b3->onClick();
        CHECK(variateButton->isEnabled() && copyButton->isEnabled(), "未锁 bar3 且下一小节未锁时 VARIATE/COPY 启用");
        {
            juce::ScopedLock lk(p.modelLock);
            p.pattern.barLocked[2] = true;
        }
        flushEditorTimers();
        CHECK(!variateButton->isEnabled(), "选中小节被锁定后 VARIATE 禁用");
        {
            juce::ScopedLock lk(p.modelLock);
            p.pattern.barLocked[2] = false;
            p.pattern.barLocked[3] = true;
        }
        flushEditorTimers();
        CHECK(variateButton->isEnabled() && !copyButton->isEnabled(), "解锁当前小节恢复 VARIATE，锁下一小节禁用 COPY");
        {
            juce::ScopedLock lk(p.modelLock);
            p.pattern.barLocked[3] = false;
        }
        flushEditorTimers();
        CHECK(copyButton->isEnabled(), "下一小节解锁后 COPY 恢复启用");
        b16->onClick();
        CHECK(!copyButton->isEnabled(), "选中 bar16 时 COPY 禁用");
    }

    // 用真实 MouseEvent 调用编辑器 mouseDown/up，不直接改写目标音来模拟点击。
    auto clickEditor = [&](juce::Point<float> position, int modifiers = 0)
    {
        const auto now = juce::Time::getCurrentTime();
        const juce::MouseEvent event(juce::Desktop::getInstance().getMainMouseSource(),
            position, juce::ModifierKeys(juce::ModifierKeys::leftButtonModifier | modifiers),
            1.f, 0.f, 0.f, 0.f, 0.f, editor.get(), editor.get(), now, position, now, 1, false);
        editor->mouseDown(event);
        editor->mouseUp(event);
    };
    if (b1 != nullptr && b1->onClick && sourceButton != nullptr)
    {
        {
            juce::ScopedLock lk(p.modelLock);
            p.pattern = controlsFixture;
            p.pattern.gridStep = 60;
            for (auto& lane : p.pattern.bars[0])
                for (auto& cell : lane) cell.velocity = 0;
            p.changed();
        }
        b1->onClick();
        flushEditorTimers();
        CHECK(!sourceButton->isEnabled(), "bar1 所有轨道力度清零后 DEVELOP 禁用");
        auto expectedClick = p.pattern;
        expectedClick.bars[0][1][0].velocity = 80;
        // 网格从 x=120 开始，4/4 的首个 60-tick 单元宽约 30.6px。
        clickEditor({135.f, 118.f + 34.f + 16.f});
        CHECK(sourceBarsEqual(p.pattern, expectedClick), "网格 y=118：点击 snare 行只写入 bar1 的 snare 首音");
        CHECK(sourceButton->isEnabled(), "鼠标重新写入 bar1 音符后 DEVELOP 立即启用");
        flushEditorTimers();
        CHECK(sourceButton->isEnabled(), "pending timers 后 DEVELOP 仍启用");
        auto* divideButton = findBarButton("/3");
        CHECK(divideButton != nullptr && divideButton->isVisible(), "真实网格点击显示细分选择控件");
        checkVisibleControls();

        CHECK(meterControl != nullptr && bool(meterControl->onChange), "拍号 ComboBox 及回调存在");
        if (meterControl != nullptr && meterControl->onChange)
        {
            meterControl->setSelectedId(4, juce::dontSendNotification);
            meterControl->onChange();
            int visibleTextEditors = 0, visibleCombos = 0, visibleTiers = 0;
            juce::TextEditor* numeratorInput = nullptr;
            juce::ComboBox* denominatorControl = nullptr;
            for (int i = 0; i < editor->getNumChildComponents(); ++i)
            {
                auto* child = editor->getChildComponent(i);
                if (!child->isVisible()) continue;
                if (auto* input = dynamic_cast<juce::TextEditor*>(child))
                    if (input->getComponentID() != "group-draft")
                    {
                        ++visibleTextEditors;
                        if (input != seedInput) numeratorInput = input;
                    }
                if (auto* combo = dynamic_cast<juce::ComboBox*>(child))
                {
                    ++visibleCombos;
                    if (combo != meterControl) denominatorControl = combo;
                }
            }
            for (const auto& group : tierButtons)
                for (const auto* button : group)
                    visibleTiers += button != nullptr && button->isVisible();
            CHECK(meterControl->getSelectedId() == 4 && visibleTextEditors == 2 && visibleCombos == 2
                  && numeratorInput != nullptr && bool(numeratorInput->onReturnKey)
                  && numeratorInput->getText() == juce::String(p.pattern.numerator)
                  && denominatorControl != nullptr && bool(denominatorControl->onChange)
                  && denominatorControl->getSelectedId() == p.pattern.denominator
                  && seedInput != nullptr && seedInput->isVisible()
                  && groupDraft != nullptr && !groupDraft->isVisible(),
                  "CUSTOM 精确显示 numerator/seed 两个文本框及 meter/denominator 两个下拉框，草稿仍隐藏");
            auto* groupSummary = dynamic_cast<juce::TextButton*>(findByID(editor.get(), "group-summary"));
            CHECK(groupSummary != nullptr && groupSummary->isVisible() && bool(groupSummary->onClick),
                  "分组摘要已是可见按钮而非旧 group 文本框，仍参与布局检查");
            CHECK(visibleTiers == 0, "CUSTOM 不显示兼容三档按钮，仍使用真实 fader");
            CHECK(divideButton != nullptr && divideButton->isVisible(),
                  "CUSTOM 布局包含真实选择后的细分控件");
            checkVisibleControls(); // 同时覆盖 CUSTOM 输入框和细分操作按钮。
            if (numeratorInput != nullptr && numeratorInput->onReturnKey
                && denominatorControl != nullptr && denominatorControl->onChange)
            {
                const auto beforeCustom = p.pattern;
                {
                    juce::ScopedLock lk(p.modelLock);
                    p.pattern.locked.fill(true);
                    p.pattern.barLocked.fill(true);
                }
                const auto sameMeterState = modelState();
                numeratorInput->onReturnKey();
                CHECK(modelState() == sameMeterState,
                      "CUSTOM 提交未改变的拍号不清音符、不重置两类锁");
                // 两次真正改拍号均从全锁状态开始，不能因草稿误定位而未执行回调。
                for (bool changeDenominator : {false, true})
                {
                    {
                        juce::ScopedLock lk(p.modelLock);
                        p.pattern.locked.fill(true);
                        p.pattern.barLocked.fill(true);
                    }
                    auto expectedMeter = p.pattern;
                    expectedMeter.meter(6, changeDenominator ? 8 : 4);
                    if (changeDenominator)
                    {
                        denominatorControl->setSelectedId(8, juce::dontSendNotification);
                        denominatorControl->onChange();
                    }
                    else
                    {
                        numeratorInput->setText("6", false);
                        numeratorInput->onReturnKey();
                    }
                    CHECK(p.pattern.numerator == 6 && p.pattern.denominator == (changeDenominator ? 8 : 4)
                          && p.pattern.groups == expectedMeter.groups && sourceBarsEqual(p.pattern, expectedMeter),
                          "CUSTOM 真实分子/分母回调改变拍号、重建分组并清空全部音符");
                    CHECK(std::none_of(p.pattern.locked.begin(), p.pattern.locked.end(), [](bool v) { return v; })
                          && std::none_of(p.pattern.barLocked.begin(), p.pattern.barLocked.end(), [](bool v) { return v; }),
                          "CUSTOM 真正改拍号严格清除全部轨道锁与全部小节锁");
                }
                {
                    juce::ScopedLock lk(p.modelLock);
                    p.pattern = beforeCustom;
                    p.changed();
                }
                flushEditorTimers();
            }
            meterControl->setSelectedId(1, juce::dontSendNotification);
            meterControl->onChange();
        }
        clickEditor({135.f, 118.f + 34.f + 16.f});
        expectedClick.bars[0][1][0].velocity = 0;
        CHECK(sourceBarsEqual(p.pattern, expectedClick) && !sourceButton->isEnabled(),
              "再次点击同一 snare 音关闭它，源小节重新为空时 DEVELOP 禁用");
        // 保留上一次网格选择；分组条点击也不能误触那个音。
        const auto beforeGroup = p.pattern;
        const auto beforeGroupState = modelState();
        for (bool shiftClick : {false, true})
        {
            clickEditor({160.f, 100.f}, shiftClick ? juce::ModifierKeys::shiftModifier : 0);
            CHECK(modelState() == beforeGroupState && p.pattern.groups == beforeGroup.groups
                  && sourceBarsEqual(p.pattern, beforeGroup),
                  shiftClick ? "分组条 Shift-click 不再减 1，完整模型及网格音符保持不变"
                             : "分组条 y=90：点击首组不再加 1，完整模型及网格音符保持不变");
            // 此控件专项没有 native peer；isShowing() 必为 false，检查可见祖先链及边界。
            CHECK(groupDraft != nullptr && groupDraft->isVisible()
                  && groupInput != nullptr && groupInput->isVisible()
                  && groupInput->getParentComponent() == groupDraft
                  && groupDraft->getLocalBounds().contains(groupInput->getBounds()),
                  "分组条点击打开草稿浮层，输入可见且完整位于浮层内");
            CHECK(groupCancel != nullptr && groupCancel->isVisible()
                  && groupCancel->isEnabled() && bool(groupCancel->onClick),
                  "分组草稿显示可用的真实 CANCEL 回调");
            if (groupCancel != nullptr && groupCancel->onClick) groupCancel->onClick();
            flushEditorTimers();
            CHECK(groupDraft != nullptr && !groupDraft->isVisible()
                  && groupInput != nullptr && !groupInput->isShowing()
                  && modelState() == beforeGroupState && sourceBarsEqual(p.pattern, beforeGroup),
                  "CANCEL 关闭草稿且 timer 同步不改模型，不遗留草稿阻断后续 DEVELOP");
        }
    }

    {
        juce::ScopedLock lk(p.modelLock);
        p.pattern = controlsFixture;
        p.changed();
    }
    p.playTick.store(-1);
    if (followButton != nullptr) followButton->setToggleState(true, juce::dontSendNotification);
    if (b9 != nullptr && b9->onClick) b9->onClick();
    flushEditorTimers(); // 同步 seed 与分组摘要，避免 REROLL 污染源发展按钮测试。
    CHECK(groupDraft != nullptr && !groupDraft->isVisible(), "源发展测试开始前分组草稿已关闭");

    // 直接执行真实按钮回调；不通过异步 triggerClick 掩盖尚未执行的操作。
    auto* developButton = findBarButton("DEVELOP BAR 1");
    CHECK(developButton != nullptr, "编辑器包含 DEVELOP BAR 1 TextButton");
    CHECK(developButton != nullptr && bool(developButton->onClick),
          "源小节发展按钮已绑定 onClick");
    if (developButton != nullptr && developButton->onClick)
        for (float dev : {0.f, .8f})
            for (bool sourceLocked : {false, true})
            {
                auto before = sourceDevelopmentFixture();
                before.groups = {3, 3, 2};
                before.development = dev;
                before.barLocked[0] = sourceLocked;
                auto expected = before;
                CHECK(expected.developFromBar1(), "按钮测试的模型期望结果可成功生成");
                {
                    juce::ScopedLock lk(p.modelLock);
                    p.pattern = before;
                    p.changed();
                }
                // 回调从 seed 文本框取值；仅同步已确认的 seed，不再遍历写入草稿或 numerator。
                if (seedInput != nullptr) seedInput->setText(juce::String(before.seed), false);
                CHECK(seedInput != nullptr && seedInput->getComponentID() != "group-draft"
                      && seedInput->getText() == juce::String(before.seed),
                      "源发展回调读取与 fixture 相同的 seed，未误写 group-draft");
                developButton->onClick();
                juce::ScopedLock lk(p.modelLock);
                bool sourceKept = true, targetsChanged = true, developed = false;
                for (int r = 0; r < lattice::tracks; ++r)
                    sourceKept &= sourceLaneEqual(p.pattern.bars[0][r], before.bars[0][r]);
                for (int b = 1; b < lattice::phraseBars; ++b)
                {
                    bool changed = false;
                    for (int r = 0; r < lattice::tracks; ++r)
                    {
                        changed |= !sourceLaneEqual(p.pattern.bars[b][r], before.bars[b][r]);
                        developed |= !sourceLaneEqual(p.pattern.bars[b][r], before.bars[0][r]);
                    }
                    targetsChanged &= changed;
                }
                CHECK(sourceKept, "真实 onClick 在锁定与未锁定时均完整保留第 1 小节");
                CHECK(targetsChanged, "真实 onClick 更新其余全部 15 小节，而非只改当前选中小节");
                CHECK(dev == 0 ? !developed : developed,
                      "真实 onClick 在 dev=0 精确复制，在 dev=.8 产生发展变化");
                CHECK(sourceBarsEqual(p.pattern, expected),
                      "真实 onClick 的全部 cells 与源发展模型结果完全一致");
            }

    editor->setVisible(false);
}

// ===========================================================================
// BACKBEAT STATE ROUNDTRIP + DEVELOPMENT RENDER TESTS (Processor.h present)
// ---------------------------------------------------------------------------
// The 0.2.5 release removed the master low-pass (no public lowpassHz); the
// 当前编辑器使用 tempo Slider 与五个参数 fader，隐藏三档按钮保留兼容。继续验证
// (a) a backbeat pattern survives a state roundtrip, and so does the default
// (backbeat=false) state; (b) a backbeat+development pattern renders finite,
// nonzero audio through the 48-voice synth (Low Tom / MIDI 41 is voiced).
// ===========================================================================
static void runBackbeatStateRoundtripTest()
{
    g_usedProcessor = true;
    section("Processor: state roundtrip preserves backbeat (and default state)");
    {
        // Backbeat state.
        LatticeProcessor proc;
        {
            juce::ScopedLock lk(proc.modelLock);
            proc.pattern.backbeat = true;
            proc.pattern.development = 0; proc.pattern.fill = 0; proc.pattern.space = 0;
            proc.pattern.generate();
            proc.changed();
        }
        juce::MemoryBlock state; proc.getStateInformation(state);

        LatticeProcessor copy; copy.setStateInformation(state.getData(), (int)state.getSize());
        CHECK(copy.pattern.backbeat == true, "state restores backbeat=true");
        CHECK(std::abs(copy.pattern.bpm - 100.0) < 1e-3, "state restores BPM=100 under backbeat");
        CHECK(sourceBarsEqual(copy.pattern, proc.pattern),
              "state 精确恢复 backbeat 的全部 cells，包括已位移主击");

        // Default state (no backbeat) still roundtrips.
        LatticeProcessor defProc;
        juce::MemoryBlock d; defProc.getStateInformation(d);
        LatticeProcessor defCopy; defCopy.setStateInformation(d.getData(), (int)d.getSize());
        CHECK(defCopy.pattern.backbeat == false, "default state: backbeat=false");
        proc.releaseResources();
    }
}

// Render a backbeat+development pattern through the built-in synth and confirm
// the output is finite and has a nonzero peak.  The model must place at least
// one Low Tom (track 2, MIDI 41) hit so the synth actually voices that drum.
static void runBackbeatDevelopmentRenderTest()
{
    g_usedProcessor = true;
    section("Processor: backbeat+development renders finite, nonzero audio (Low Tom MIDI 41 in model)");
    {
        LatticeProcessor proc;
        {
            juce::ScopedLock lk(proc.modelLock);
            proc.pattern.backbeat = true;
            proc.pattern.development = 0.8f; proc.pattern.fill = 0; proc.pattern.space = 0;
            proc.pattern.generate();
            bool lowTomHit = false;
            for (int b = 0; b < lattice::phraseBars && !lowTomHit; ++b)
                for (const auto& c : proc.pattern.bars[b][2])
                    if (c.velocity > 0) { lowTomHit = true; break; }
            CHECK(lowTomHit, "model: Low Tom (track 2, MIDI 41) is generated by backbeat development");
            proc.changed();
        }
        proc.prepareToPlay(48000, 256);
        proc.preview.store(true);
        float peak = 0.0f; bool finite = true;
        for (int k = 0; k < 800; ++k)
        {
            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            proc.processBlock(buf, midi);
            for (int c = 0; c < buf.getNumChannels(); ++c)
                for (int i = 0; i < buf.getNumSamples(); ++i)
                {
                    const float v = buf.getSample(c, i);
                    if (!std::isfinite(v)) finite = false;
                    const float a = std::fabs(v);
                    if (a > peak) peak = a;
                }
        }
        proc.preview.store(false);
        proc.releaseResources();
        CHECK(finite, "backbeat+development render produces finite output");
        CHECK(peak > 0.0f, "backbeat+development render has nonzero peak");
        std::printf("        (peak=%.4f, finite=%d)\n", peak, (int)finite);
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

// JUCE宿主state是VST3PluginState/IComponent，不可把裸LATTICE XML直接塞给VST3实例。
#if __has_include("Processor.h")
static bool injectGrooveState(juce::AudioPluginInstance& plugin, const lattice::Pattern& pattern)
{
    LatticeProcessor source;
    source.pattern = pattern;
    juce::MemoryBlock raw, outer;
    source.getStateInformation(raw);
    plugin.getStateInformation(outer);
    auto wrapper = juce::AudioProcessor::getXmlFromBinary(outer.getData(), int(outer.getSize()));
    if (!wrapper || !wrapper->hasTagName("VST3PluginState")) return false;
    auto* component = wrapper->getChildByName("IComponent");
    if (!component) return false;
    component->deleteAllChildElements();
    component->addTextElement(raw.toBase64Encoding());
    juce::AudioProcessor::copyXmlToBinary(*wrapper, outer);
    plugin.setStateInformation(outer.getData(), int(outer.getSize()));

    // 必须读回实际插件的所有参数/cells；防止注入被拒却仍播放默认鼓句的假阳性。
    plugin.getStateInformation(outer);
    wrapper = juce::AudioProcessor::getXmlFromBinary(outer.getData(), int(outer.getSize()));
    if (!wrapper) return false;
    component = wrapper->getChildByName("IComponent");
    juce::MemoryBlock restored;
    if (!component || !restored.fromBase64Encoding(component->getAllSubText())) return false;
    auto actual = juce::AudioProcessor::getXmlFromBinary(restored.getData(), int(restored.getSize()));
    auto expected = juce::AudioProcessor::getXmlFromBinary(raw.getData(), int(raw.getSize()));
    return actual && expected && actual->hasTagName("LATTICE") && actual->toString() == expected->toString();
}
#endif

static void runGrooveAudioTests(const juce::String& vst3)
{
#if __has_include("Processor.h")
    section("Groove音频：真实VST3、state注入、16小节事件对照与Kick隔离");
    // preview不在当前state中序列化。使用已有TestTransport走真实插件的host播放路径；
    // 参考端仅收逐事件MIDI，关闭preview且不接host，避免两个相同调度错误互相通过。
    constexpr double sampleRate = 48000., bpm = 100.;
    constexpr int blockSize = 256, samplesPerTick = 60;
    constexpr int samplesPerBar = 115200, totalSamples = samplesPerBar * lattice::phraseBars;
    lattice::Pattern generated;
    generated.seed = 1729; generated.bpm = bpm; generated.development = .8f;
    generated.fill = 1; generated.space = .75f; generated.dispersion = 2;
    const bool generatedOK = generated.generate();
    CHECK(generatedOK, "音频fixture由当前generate产生：4/4、100BPM、16小节");
    if (!generatedOK) return;
    CHECK(std::abs(totalSamples / sampleRate - 38.4) < 1.e-9,
          "每次连续渲染1843200帧，恰好16小节/38.4秒，不以短瞬态替代整句");

    for (bool kickOnly : {false, true})
    {
        auto pattern = generated;
        if (kickOnly) for (auto& bar : pattern.bars) for (int r = 1; r < lattice::tracks; ++r)
            for (auto& cell : bar[r]) cell.velocity = 0;
        // 两次使用新实例，避免整鼓尾音串入Kick隔离结果；不向真实插件发送测试MIDI。
        TestTransport host; host.bpm = bpm; host.playing = true;
        juce::String error;
        auto plugin = loadVst3(vst3, error);
        CHECK(plugin != nullptr, "加载真实VST3实例成功，不以内置Processor代替被测插件");
        if (!plugin) { std::fprintf(stderr, "  VST3加载失败：%s\n", error.toRawUTF8()); continue; }
        const bool injected = injectGrooveState(*plugin, pattern);
        CHECK(injected, "真实VST3完整state注入并读回一致（Kick隔离时其他七轨逐cell为零）");
        if (!injected) continue;
        plugin->setPlayHead(&host);
        plugin->setNonRealtime(true);
        plugin->prepareToPlay(sampleRate, blockSize);
        CHECK(plugin->getTotalNumOutputChannels() == 2, "真实VST3具有双声道输出");
        if (plugin->getTotalNumOutputChannels() != 2) {
            plugin->releaseResources(); plugin->setPlayHead(nullptr); continue;
        }
        LatticeProcessor reference;
        reference.preview.store(false);
        reference.setPlayHead(nullptr);
        reference.prepareToPlay(sampleRate, blockSize);
        struct Event { int sample, row, velocity, bar; };
        std::vector<Event> events;
        std::array<int, lattice::phraseBars> expectedEvents{}, sentEvents{}, kickEvents{};
        for (int b = 0; b < lattice::phraseBars; ++b) for (int r = 0; r < lattice::tracks; ++r)
            for (const auto& cell : pattern.bars[b][r]) if (cell.velocity > 0) {
                events.push_back({(b * pattern.barTicks() + cell.start) * samplesPerTick, r, cell.velocity, b});
                ++expectedEvents[b]; kickEvents[b] += r == 0;
            }
        std::sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return a.sample < b.sample || (a.sample == b.sample && a.row < b.row);
        });
        juce::AudioBuffer<float> rendered(2, totalSamples);
        std::array<bool, lattice::phraseBars> finite{}; finite.fill(true);
        std::array<float, lattice::phraseBars> peaks{}, errors{};
        std::array<double, lattice::phraseBars> energy{};
        size_t eventIndex = 0;
        int processed = 0;
        while (processed < totalSamples) {
            const int count = std::min(blockSize, totalSamples - processed);
            juce::AudioBuffer<float> audio(2, count), expected(2, count);
            juce::MidiBuffer empty, midi;
            host.ppq = double(processed) * bpm / (60. * sampleRate);
            while (eventIndex < events.size() && events[eventIndex].sample < processed + count) {
                const auto& e = events[eventIndex++];
                midi.addEvent(juce::MidiMessage::noteOn(10, kGM[e.row], juce::uint8(e.velocity)), e.sample - processed);
                ++sentEvents[e.bar];
            }
            plugin->processBlock(audio, empty);
            reference.processBlock(expected, midi);
            for (int ch = 0; ch < 2; ++ch) {
                rendered.copyFrom(ch, processed, audio, ch, 0, count);
                for (int i = 0; i < count; ++i) {
                    const int bar = (processed + i) / samplesPerBar;
                    const float x = audio.getSample(ch, i), ref = expected.getSample(ch, i);
                    finite[bar] &= std::isfinite(x) && std::isfinite(ref);
                    peaks[bar] = std::max(peaks[bar], std::abs(x));
                    errors[bar] = std::max(errors[bar], std::abs(x - ref));
                    energy[bar] += double(x) * x;
                }
            }
            processed += count;
        }
        CHECK(processed == totalSamples && eventIndex == events.size(), "连续处理覆盖全部16小节和全部参考事件，无跳bar或提前终止");
        for (int b = 0; b < lattice::phraseBars; ++b) {
            std::printf("        %s bar=%02d events=%d kick=%d peak=%.6f rms=%.6f maxError=%.8f\n",
                        kickOnly ? "Kick隔离" : "整鼓", b + 1, expectedEvents[b], kickEvents[b], peaks[b],
                        std::sqrt(energy[b] / (2 * samplesPerBar)), errors[b]);
            CHECK(finite[b] && peaks[b] > .0001f && peaks[b] <= 1.f,
                  "本小节双声道全程无NaN/Inf且有非零、有界输出");
            CHECK(expectedEvents[b] > 0 && kickEvents[b] > 0 && sentEvents[b] == expectedEvents[b] && errors[b] < .00002f,
                  "本小节真实插件波形与逐事件MIDI参考一致，非仅检查模型事件或总peak");
            recordPeak(peaks[b]);
        }
        if (kickOnly) {
            // 每个Kick触发后的短窗都必须有实际输出，不能用之前镲尾音或全句峰值蒙混。
            for (int b = 0; b < lattice::phraseBars; ++b) {
                bool audible = true; int checked = 0;
                for (const auto& e : events) if (e.bar == b) {
                    const int window = std::min(2400, totalSamples - e.sample);
                    audible &= e.row == 0 && window > 0 && rendered.getMagnitude(e.sample, window) > .0001f;
                    ++checked;
                }
                CHECK(audible && checked == kickEvents[b] && checked > 0,
                      "Kick隔离本小节每次触发后50ms真实输出非零，全部16bar采样链不断");
            }
        }
        // 停止host后下一块不再发声，确保整句声音来自受控播放而非插件默认自运行。
        host.playing = false;
        juce::AudioBuffer<float> stopped(2, blockSize); juce::MidiBuffer empty;
        plugin->processBlock(stopped, empty);
        CHECK(stopped.getMagnitude(0, blockSize) < .000001f, "host停止后真实VST3静音，不依赖隐藏preview或外部MIDI");
        plugin->releaseResources(); plugin->setPlayHead(nullptr); reference.releaseResources();
    }
    std::printf("[音频边界] 整鼓与Kick隔离各16小节/38.4秒，数值验收；无主观听验，不写WAV或试听网页。\n");
#else
    juce::ignoreUnused(vst3);
    CHECK(false, "groove音频专项需要Processor state序列化和MIDI参考支持");
#endif
}

static void runSamplePlaybackTests(juce::AudioProcessor& processor)
{
    section("Sampler: exact source WAV mapping, stereo, velocity, rate conversion and tails");
    const char* files[]={"kick.wav","snare.wav","tom2.wav","hat.wav","open.wav","tom1.wav","ride.wav","crash.wav"};
    const float pan[]={0,0,-.15f,-.25f,-.2f,.2f,.3f,.1f};
    const auto folder=juce::File(__FILE__).getParentDirectory().getParentDirectory().getChildFile("src/samples");
    juce::WavAudioFormat wav;
    for(int row=0;row<8;++row){
        std::unique_ptr<juce::AudioFormatReader> reader(wav.createReaderFor(folder.getChildFile(files[row]).createInputStream().release(),true));
        CHECK(reader!=nullptr,"reference WAV opens");if(!reader)continue;
        juce::AudioBuffer<float> source(2,int(reader->lengthInSamples));
        CHECK(reader->read(&source,0,source.getNumSamples(),0,true,true),"reference WAV decodes");
        for(double sr:{44100.,48000.,96000.})for(int velocity:{40,80,127}){
            processor.releaseResources();processor.prepareToPlay(sr,257);
            const int offset=17,total=offset+int(std::ceil(source.getNumSamples()*sr/reader->sampleRate))+514;
            bool matches=true,finite=true;float peak=0,maxError=0;
            for(int start=0;start<total;start+=257){
                juce::AudioBuffer<float> audio(2,257);juce::MidiBuffer midi;
                if(start==0)midi.addEvent(juce::MidiMessage::noteOn(10,kGM[row],juce::uint8(velocity)),offset);
                processor.processBlock(audio,midi);
                for(int i=0;i<257;++i)for(int ch=0;ch<2;++ch){
                    const double pos=(start+i-offset)*reader->sampleRate/sr;float expected=0;
                    if(pos>=0&&pos<source.getNumSamples()){
                        const int index=int(pos);const float f=float(pos-index),a=source.getSample(ch,index);
                        const float b=index+1<source.getNumSamples()?source.getSample(ch,index+1):0.f;
                        const float gain=ch==0?1.f-pan[row]:1.f+pan[row];
                        expected=std::tanh((a+(b-a)*f)*(float(velocity)/127.f)*gain*.65f);
                    }
                    const float actual=audio.getSample(ch,i);finite &= std::isfinite(actual);
                    maxError=std::max(maxError,std::abs(actual-expected));peak=std::max(peak,std::abs(actual));
                }
            }
            matches=maxError<.00001f;
            CHECK(matches&&finite&&peak>0,"entire stereo output matches source WAV, including MIDI offset and full tail");
            if(!matches)std::printf("        mismatch %s %.0fHz vel=%d error=%.8f\n",files[row],sr,velocity,maxError);
        }
    }
    section("Sampler: closed hat chokes open hat; all-sound-off clears voices");
    processor.releaseResources();processor.prepareToPlay(48000,257);
    LatticeProcessor closedOnly;closedOnly.prepareToPlay(48000,257);
    juce::AudioBuffer<float> both(2,257),closed(2,257);juce::MidiBuffer first;
    first.addEvent(juce::MidiMessage::noteOn(10,kGM[4],juce::uint8(127)),0);processor.processBlock(both,first);
    bool chokeMatches=true;
    for(int b=0;b<8;++b){
        juce::MidiBuffer a,m;if(b==0){a.addEvent(juce::MidiMessage::noteOn(10,kGM[3],juce::uint8(80)),13);m=a;}
        processor.processBlock(both,a);closedOnly.processBlock(closed,m);
        for(int i=b==0?13:0;i<257;++i)for(int ch=0;ch<2;++ch)chokeMatches &= std::abs(both.getSample(ch,i)-closed.getSample(ch,i))<.000001f;
    }
    CHECK(chokeMatches,"closed hat cuts only open-hat voices at the exact MIDI offset");
    juce::MidiBuffer stop;stop.addEvent(juce::MidiMessage::allSoundOff(10),0);processor.processBlock(both,stop);
    CHECK(both.getMagnitude(0,both.getNumSamples())==0,"all-sound-off leaves no sampler tail");
    processor.releaseResources();
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

    runSamplePlaybackTests(*instance);
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
            CHECK(img.isValid() && img.getWidth() == 1120 && img.getHeight() == 760,
                  "GUI 截图有效且保持确认版 1120x760 尺寸");
            if (img.isValid())
            {
                const auto background = img.getPixelAt(10, 10);
                CHECK(background.getAlpha() == 255 && background.getBrightness() < 0.2f,
                      "GUI 截图边角为不透明深色背景");
                int sampled = 0, dark = 0, brightTop = 0, brightBottom = 0;
                for (int y = 0; y < img.getHeight(); y += 4)
                    for (int x = 0; x < img.getWidth(); x += 4)
                    {
                        const auto pixel = img.getPixelAt(x, y);
                        ++sampled;
                        dark += pixel.getAlpha() == 255 && pixel.getBrightness() < 0.3f;
                        if (pixel.getAlpha() == 255 && pixel.getBrightness() > 0.6f)
                        {
                            if (y < 448) ++brightTop;
                            else ++brightBottom;
                        }
                    }
                CHECK(dark > sampled * 3 / 4, "GUI 截图主体为深色界面");
                CHECK(brightTop > 50 && brightBottom > 50,
                      "GUI 上方序列器与下方参数区均有足量亮色内容，排除空白或纯色截图");
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
    bool gui = false, samplesOnly = false, editorOnly = false, groupsTomOnly = false;
    bool grooveOnly = false, grooveAudioOnly = false, wavOnly = false;
    juce::String vst3;
    for (int i = 1; i < argc; ++i)
    {
        juce::String a = argv[i];
        if (a == "--gui")            gui = true;
        else if (a == "--samples-only") samplesOnly = true;
        else if (a == "--editor-only") editorOnly = true;
        else if (a == "--groups-tom-only") groupsTomOnly = true;
        else if (a == "--groove-only") grooveOnly = true;
        else if (a == "--groove-audio-only") grooveAudioOnly = true;
        else if (a == "--wav-only") wavOnly = true;
        else if (a == "--vst3" && i + 1 < argc) vst3 = argv[++i];
        else if (!a.startsWith("--")) vst3 = a;   // positional .vst3 path
    }

    // Initialises the MessageManager + native GUI; lives until main returns so the
    // GUI/dispatch-loop test can use it.
    juce::ScopedJuceInitialiser_GUI guiInit;

    std::printf("=== LATTICE Tests ===\n");
    if (wavOnly)
    {
        const bool exclusive = !gui && !samplesOnly && !editorOnly && !groupsTomOnly
            && !grooveOnly && !grooveAudioOnly && vst3.isEmpty();
        CHECK(exclusive, "--wav-only 不可混用其他专项、GUI 或 VST3 路径");
        if (!exclusive) return 1;
#if __has_include("Processor.h")
        runWavExportTests();
#else
        CHECK(false, "--wav-only 需要真实 Processor.h 实现");
#endif
        std::printf("\n=== WAV 专项汇总（非全量）===\nPASS=%d  FAIL=%d  PEAK=%.4f\n", g_pass, g_fail, g_peak);
        return g_fail > 0 ? 1 : 0;
    }
    if (grooveOnly || grooveAudioOnly)
    {
        const bool exclusive = int(grooveOnly) + int(grooveAudioOnly) + int(samplesOnly)
            + int(editorOnly) + int(groupsTomOnly) == 1 && !gui && (!grooveOnly || vst3.isEmpty());
        CHECK(exclusive, "groove专项不可混用其他专项/GUI；真实VST3请使用groove-audio-only");
        if (!exclusive) return 1;
        std::printf("[边界] 仅验收当前专项；不执行旧Group carriers/固定backbeat断言，不代表旧全量通过。\n");
        if (grooveOnly)
        {
            runGroovePhraseTests();
            runCoreRhythmTests();
            runTomPhraseTests();
            runSourceDevelopmentTests();
#if __has_include("Processor.h")
            runProcessorMidiTests();
            runStateVersionTests();
            runTransportTests();
            runBackbeatStateRoundtripTest(); // 仅state兼容，不调用旧backbeat生成/固定LowTom断言。
#else
            CHECK(false, "groove专项要求独立Processor MIDI/state/transport测试可用");
#endif
        }
        else
        {
            CHECK(vst3.isNotEmpty(), "groove-audio-only必须提供真实 --vst3 路径");
            if (vst3.isNotEmpty()) runGrooveAudioTests(vst3);
        }
        std::printf("\n=== %s 专项汇总（非全量）===\nPASS=%d  FAIL=%d  PEAK=%.4f\n",
                    grooveOnly ? "GROOVE" : "GROOVE AUDIO", g_pass, g_fail, g_peak);
        return g_fail > 0 ? 1 : 0;
    }
    if(groupsTomOnly){
        runTomPhraseTests();runGroupEditorTests();runGroupingSemanticsTests();runDispersionTests();
        std::printf("\n=== GROUPS/TOM SUMMARY ===\nPASS=%d  FAIL=%d\n",g_pass,g_fail);
        return g_fail>0?1:0;
    }
    if(samplesOnly){
        CHECK(vst3.isNotEmpty(),"sampler validation requires an actual VST3 path");
        if(vst3.isNotEmpty())runPluginLoadTest(vst3);
        std::printf("\n=== SAMPLER SUMMARY ===\nPASS=%d  FAIL=%d\n",g_pass,g_fail);
        return g_fail>0?1:0;
    }
    if (editorOnly)
    {
        if (gui)
        {
            CHECK(vst3.isNotEmpty(), "--editor-only --gui 需要 --vst3 路径或位置参数路径");
            if (vst3.isNotEmpty()) runGuiTest(vst3);
        }
        else
        {
#if __has_include("Processor.h")
            runEditorControlsTest();
#else
            CHECK(false, "--editor-only 控件专项需要 Processor.h");
#endif
        }
        std::printf("\n=== EDITOR SUMMARY ===\nPASS=%d  FAIL=%d\n", g_pass, g_fail);
        return g_fail > 0 ? 1 : 0;
    }
    runModelTests();
    {
        bool safe=true;
        for(int n=1;n<=16;++n)for(int d:{4,8})for(bool bb:{false,true})for(int seed=0;seed<8;++seed){
            lattice::Pattern p;p.meter(n,d);p.backbeat=bb;p.seed=seed;p.ghost=false;p.accents=false;p.development=.8f;p.fill=1;p.generate();
            for(int b=0;b<16;++b)for(int r=0;r<8;++r){int end=0;for(auto c:p.bars[b][r]){safe &= c.start==end && (c.velocity==0||c.velocity==80);end+=c.length;}safe &= end==p.barTicks();}
        }
        CHECK(safe,"512 meter/mode/seed cases: contiguous grid, no ghost/accent when disabled");
    }
    runGroupingSemanticsTests();
    runGroupingSweepTests();
    runOpenCloseTests();
    runBackbeatTests();
    runBackbeatDevelopmentModelTests();
    runSourceDevelopmentTests();
    runDispersionTests();
    runRidePlanningTests();
    {
        bool noClash=true;
        for(bool bb:{false,true})for(float dev:{0.f,.45f,.8f})for(int seed=0;seed<60;++seed){
            lattice::Pattern p;p.backbeat=bb;p.development=dev;p.seed=seed;p.generate();
            for(int b=0;b<16;++b)for(int t=0;t<p.barTicks();t+=60)
                if(velAt(p,b,0,t)>=80&&velAt(p,b,1,t)>=80)noClash=false;
        }
        CHECK(noClash,"360 patterns: no simultaneous strong kick/snare in either groove mode");
    }
    section("Model: cross-bar phrases and displaced accents");
    for (float dev : {0.45f, 0.8f})
    {
        bool bridge = true, silence = true, displaced = true, deterministic = true;
        for (int seed = 1; seed <= 60; ++seed)
        {
            lattice::Pattern p; p.seed = seed; p.development = dev; p.generate();
            lattice::Pattern q = p; q.generate();
            const int ticks = p.barTicks();
            for(int base:{0,8}){
                bool join=false;
                for(int split=base+2;split<=base+5;++split)
                    join |= (velAt(p,split-1,1,ticks-120)>=80 || velAt(p,split-1,5,ticks-120)>=80) && velAt(p,split,1,120)>=40;
                bridge &= join;
                int rest=std::min(ticks/60-1,int(p.space*12)*2)*60;
                for(int t=ticks-rest;t<ticks;t+=60)for(int r=0;r<8;++r)silence &= velAt(p,base+7,r,t)==0;
            }
            lattice::Pattern source=p;source.development=0;source.fill=0;source.space=0;source.generate();
            int retained = 0, anchors = 0;
            for (int b = 0; b < lattice::phraseBars; ++b)
                for (const auto& c : source.bars[b % 2][1]) if (c.velocity == 127)
                {
                    ++anchors;
                    retained += velAt(p, b, 1, c.start) == c.velocity;
                }
            // 按已位移的两个源 motif 分别追踪整句多数继承，不再固定 bar0 锚点或十小节。
            displaced &= anchors > 0 && retained * 2 > anchors;
            if (!(anchors > 0 && retained * 2 > anchors))
                std::printf("        retention dev=%.2f seed=%d retained=%d/%d\n", double(dev), seed, retained, anchors);
            for (int b = 0; b < lattice::phraseBars; ++b)
                for (int r = 0; r < lattice::tracks; ++r)
                    for (int t = 0; t < ticks; t += 60)
                        deterministic &= velAt(p,b,r,t) == velAt(q,b,r,t);
        }
        CHECK(bridge, "60 seeds: pickup before bar line and delayed snare answer after it");
        CHECK(silence, "60 seeds: cross-line joins never refill reserved phrase-end rests");
        CHECK(displaced, "60 seeds：按两个已位移 motif 追踪军鼓主击，整句聚合保留多数");
        CHECK(deterministic, "60 seeds: long phrase regeneration remains deterministic");
    }

#if __has_include("Processor.h")
    runProcessorMidiTests();
    runTransportTests();
    runStateVersionTests();
    runEditorControlsTest();
    {
        section("Processor: source development state and audible playback");
        LatticeProcessor proc;proc.pattern=sourceDevelopmentFixture();
        for(auto& bar:proc.pattern.bars)for(auto& lane:bar)for(auto& c:lane)
            if(c.velocity)c.velocity=c.velocity>=100?127:c.velocity>=60?80:40;
        proc.pattern.development=.8f;
        CHECK(proc.pattern.developFromBar1(),"source development succeeds for persisted velocity palette");
        proc.changed();juce::MemoryBlock state;proc.getStateInformation(state);
        LatticeProcessor restored;restored.setStateInformation(state.getData(),int(state.getSize()));
        CHECK(sourceBarsEqual(proc.pattern,restored.pattern),"developed cells and triplet timing survive state reload");
        auto exported=restored.exportMidi();juce::FileInputStream midiInput(exported);juce::MidiFile midiFile;
        bool midiOK=midiFile.readFrom(midiInput);int exportedHits=0,expectedHits=0;bool triplet=false;
        for(const auto& bar:restored.pattern.bars)for(const auto& lane:bar)for(const auto& c:lane)expectedHits+=c.velocity>0;
        if(midiOK)for(int t=0;t<midiFile.getNumTracks();++t)for(int e=0;e<midiFile.getTrack(t)->getNumEvents();++e){
            const auto& msg=midiFile.getTrack(t)->getEventPointer(e)->message;
            if(msg.isNoteOn()){++exportedHits;triplet |= int(msg.getTimeStamp())%60!=0;}}
        CHECK(midiOK&&exportedHits==expectedHits&&triplet,"source-derived MIDI preserves all note-ons including off-grid triplet events");
        restored.prepareToPlay(44100,256);restored.preview.store(true);
        bool finite=true;float peak=0;
        for(int block=0;block<1000;++block){juce::AudioBuffer<float> audio(2,256);juce::MidiBuffer midi;restored.processBlock(audio,midi);
            for(int ch=0;ch<2;++ch)for(int i=0;i<256;++i){float x=audio.getSample(ch,i);finite &= std::isfinite(x);peak=std::max(peak,std::abs(x));}}
        CHECK(finite&&peak>0.001f&&peak<=1.f,"restored source development plays finite nonzero audio across several bars");
        restored.releaseResources();
    }
    runBackbeatStateRoundtripTest();
    runBackbeatDevelopmentRenderTest();
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
