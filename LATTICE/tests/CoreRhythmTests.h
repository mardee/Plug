#pragma once
#include "GroovePhraseTests.h"
#include <array>
#include <cstdlib>
#include <set>
#include <string>
#include <vector>

namespace core_rhythm_test {
// 对齐 generate 的 CORE 随机寻址，仅用于端到端逐cell接线对照；位移指标直接来自实际输出。
inline unsigned coreKey(int seed)
{
    unsigned x = static_cast<unsigned>(seed) ^ 0x434f5245u;
    x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
    return x;
}
inline std::vector<int> edges(const lattice::Pattern& p)
{
    std::vector<int> result{0};
    for (int group : p.groups) result.push_back(result.back() + group * 4);
    return result;
}
inline bool identity(const lattice::CoreMotif& a, const lattice::CoreMotif& b, bool positions = false)
{
    if (a.steps != b.steps || a.grammar != b.grammar || a.windows.size() != b.windows.size()
        || a.hits.size() != b.hits.size()) return false;
    for (size_t i = 0; i < a.windows.size(); ++i)
        if (a.windows[i].begin != b.windows[i].begin || a.windows[i].end != b.windows[i].end) return false;
    // 下标就是身份，不能排序后用数量/力度直方图掩盖事件互换。
    for (size_t i = 0; i < a.hits.size(); ++i)
        if (a.hits[i].voice != b.hits[i].voice || a.hits[i].velocity != b.hits[i].velocity
            || a.hits[i].phrase != b.hits[i].phrase || (positions && a.hits[i].step != b.hits[i].step)) return false;
    return true;
}
inline bool safe(const lattice::CoreMotif& core, int tier = 0)
{
    if (core.steps <= 0 || core.windows.empty() || core.hits.empty()) return false;
    int end = 0, firstKick = core.steps;
    std::array<std::set<int>, 2> occupied;
    for (const auto& w : core.windows) {
        if (w.begin != end || w.end <= w.begin || w.end > core.steps) return false;
        end = w.end;
    }
    if (end != core.steps) return false;
    std::vector<int> calls(core.windows.size(), 0), replies(core.windows.size(), 0);
    std::vector<int> lastCall(core.windows.size(), -1), firstReply(core.windows.size(), core.steps);
    for (const auto& h : core.hits) {
        if (h.voice < 0 || h.voice > 1 || h.phrase < 0 || h.phrase >= int(core.windows.size())
            || h.step < 0 || h.step >= core.steps || (h.velocity != 80 && h.velocity != 127)) return false;
        if (!occupied[h.voice].insert(h.step).second) return false;
        const auto& w = core.windows[size_t(h.phrase)];
        if (h.voice == 0) {
            if (h.step < std::max(0, w.begin - 4) || h.step > w.end - 2) return false;
            ++calls[size_t(h.phrase)];
            lastCall[size_t(h.phrase)] = std::max(lastCall[size_t(h.phrase)], h.step);
            firstKick = std::min(firstKick, h.step);
        } else {
            // 仅整句>8且High的窄窗允许1step微位置，其余档位保留2step边界。
            const int margin = tier == 2 && core.steps > 8 && w.end - w.begin <= 8 ? 1 : 2;
            if (h.step < w.begin + margin || h.step > w.end - margin) return false;
            ++replies[size_t(h.phrase)];
            firstReply[size_t(h.phrase)] = std::min(firstReply[size_t(h.phrase)], h.step);
        }
    }
    for (size_t i = 0; i < core.windows.size(); ++i) {
        const auto& w = core.windows[i];
        const int gap = tier == 2 && core.steps > 8 && w.end - w.begin <= 8 ? 1 : 2;
        if (calls[i] < 1 || calls[i] > 2 || replies[i] != 1 || firstReply[i] < lastCall[i] + gap) return false;
    }
    return firstKick >= 0 && firstKick * 60 <= 240;
}
inline bool callSpacing(const lattice::CoreMotif& a, const lattice::CoreMotif& b)
{
    if (!identity(a, b)) return false;
    for (size_t i = 0; i < a.hits.size(); ++i) for (size_t j = i + 1; j < a.hits.size(); ++j)
        if (a.hits[i].voice == 0 && a.hits[j].voice == 0 && a.hits[i].phrase == a.hits[j].phrase
            && a.hits[j].step - a.hits[i].step != b.hits[j].step - b.hits[i].step) return false;
    return true;
}
inline bool matches(const lattice::Pattern& p, int bar, const lattice::CoreMotif& core)
{
    for (int voice : {0, 1}) {
        lattice::Lane expected;
        for (int t = 0; t < p.barTicks(); t += 60) expected.push_back({t, 60, 0});
        for (const auto& h : core.hits) if (h.voice == voice) {
            if (h.step < 0 || h.step >= int(expected.size())) return false;
            if (expected[size_t(h.step)].velocity != 0) return false;
            expected[size_t(h.step)].velocity = h.velocity;
        }
        if (!groove_test::sameLane(expected, p.bars[bar][voice])) return false;
    }
    return true;
}
inline std::vector<lattice::Cell> active(const lattice::Lane& lane)
{
    std::vector<lattice::Cell> result;
    for (const auto& c : lane) if (c.velocity > 0) result.push_back(c);
    return result;
}
inline std::string structure(const std::vector<lattice::Cell>& cells)
{
    std::string key;
    for (const auto& c : cells) key += std::to_string(c.start) + ",";
    return key;
}
struct Stats {
    int total = 0, movedMid = 0, movedHigh = 0, majoritySeeds = 0, highMidSeeds = 0, nonUniformSeeds = 0;
    long long displacementMid = 0, displacementHigh = 0;
    std::array<std::set<std::string>, 3> unique;
};
}

inline void runCoreRhythmTests()
{
    using namespace core_rhythm_test;
    section("CoreRhythm：64seed × 两模式 × 两motif，Low/Mid/High逐身份与真实bars");
    for (bool bb : {false, true}) {
        std::array<std::array<Stats, 2>, 2> stats{};
        bool accepted = true, conserved = true, bounded = true, pure = true;
        bool integrated = true, repeated = true, deterministic = true, realIdentity = true, lowUnchanged = true;
        for (int seed = 0; seed < 64; ++seed) {
            lattice::Pattern base;
            base.seed = seed; base.backbeat = bb; base.ghost = false;
            base.fill = 0; base.density = 0; base.development = 0; base.space = 0;
            base.accents = true;
            std::array<lattice::Pattern, 3> generated{base, base, base};
            for (int tier : {0, 1, 2}) {
                auto& p = generated[size_t(tier)]; p.dispersion = tier;
                accepted &= p.generate(); bounded &= groove_test::safeGridAndLimbs(p);
                auto again = p; accepted &= again.generate(); deterministic &= groove_test::sameBars(p, again);
                for (int b = 0; b < lattice::phraseBars; ++b) for (int voice = 0; voice < lattice::tracks; ++voice)
                    repeated &= groove_test::sameLane(p.bars[b][voice], p.bars[b % 2][voice]);
            }
            for (int member : {0, 1}) {
                const auto source = lattice::buildCoreMotif(edges(base), coreKey(seed), member, bb);
                const auto snapshot = source;
                bounded &= safe(source);
                for (int tier : {0, 1, 2}) {
                    const auto voiced = lattice::voiceRhythm(source, tier);
                    conserved &= identity(source, voiced) && callSpacing(source, voiced);
                    bounded &= safe(voiced, tier);
                    pure &= identity(source, snapshot, true)
                        && identity(voiced, lattice::voiceRhythm(source, tier), true);
                    if (tier == 0) lowUnchanged &= identity(source, voiced, true);
                    for (int b = member; b < lattice::phraseBars; b += 2)
                        integrated &= matches(generated[size_t(tier)], b, voiced);
                }
                for (int voice : {0, 1}) {
                    auto& s = stats[size_t(member)][size_t(voice)];
                    std::array<std::vector<lattice::Cell>, 3> actual;
                    for (int tier : {0, 1, 2}) {
                        actual[size_t(tier)] = active(generated[size_t(tier)].bars[member][voice]);
                        s.unique[size_t(tier)].insert(structure(actual[size_t(tier)]));
                    }
                    if (actual[0].empty() || actual[0].size() != actual[1].size() || actual[0].size() != actual[2].size()) {
                        realIdentity = false; continue;
                    }
                    int moved = 0;
                    std::set<int> highOffsets;
                    bool highMid = false;
                    for (size_t i = 0; i < actual[0].size(); ++i) {
                        const auto& low = actual[0][i]; const auto& mid = actual[1][i]; const auto& high = actual[2][i];
                        realIdentity &= low.velocity == mid.velocity && low.velocity == high.velocity
                            && low.length == mid.length && low.length == high.length;
                        const int dm = mid.start - low.start, dh = high.start - low.start;
                        ++s.total; s.movedMid += dm != 0; s.movedHigh += dh != 0; moved += dh != 0;
                        s.displacementMid += std::abs(dm); s.displacementHigh += std::abs(dh);
                        highOffsets.insert(dh); highMid |= high.start != mid.start;
                    }
                    s.majoritySeeds += moved * 2 >= int(actual[0].size());
                    s.highMidSeeds += highMid;
                    s.nonUniformSeeds += highOffsets.size() > 1;
                }
            }
        }
        CHECK(accepted && deterministic && pure && lowUnchanged, "合法生成可重现，helper不改源、Low恒等且调用确定");
        CHECK(conserved && realIdentity, "两motif三档逐身份数量/voice/力度/phrase不变，双Kick问句内部间距不变");
        CHECK(bounded, "同声部无碰撞、问答窗口有界、回答晚于问句、首Kick在0..240tick且真实bars网格安全");
        CHECK(integrated, "ghost=false fill=0 density=0 dev=0：真实16小节Kick/Snare逐cell等于对应主干，无额外击打或丢失");
        CHECK(repeated, "dev=0三档八轨完整16小节重复两motif，含Crash");
        for (int member : {0, 1}) for (int voice : {0, 1}) {
            const auto& s = stats[size_t(member)][size_t(voice)];
            std::printf("        mode=%d motif=%d %s：Mid移动=%d/%d(%.1f%%) High移动=%d/%d(%.1f%%)；"
                        "High移动>=50%% seeds=%d/64；非统一平移=%d/64；High!=Mid=%d/64；"
                        "aggregate位移tick Mid=%lld High=%lld；unique位置结构 L/M/H=%zu/%zu/%zu\n",
                        int(bb), member, lattice::names[voice], s.movedMid, s.total,
                        s.total ? 100.0 * s.movedMid / s.total : 0.0, s.movedHigh, s.total,
                        s.total ? 100.0 * s.movedHigh / s.total : 0.0, s.majoritySeeds,
                        s.nonUniformSeeds, s.highMidSeeds, s.displacementMid, s.displacementHigh,
                        s.unique[0].size(), s.unique[1].size(), s.unique[2].size());
            CHECK(s.total > 0 && s.majoritySeeds > 32, "每模式每motif每声部：多数seed的High相对Low至少50%主击发生位移");
            CHECK(s.nonUniformSeeds == 64, "每声部每seed的High均非整轨统一平移，至少存在两种身份位移量");
            // 部分语法的回答经限幅可重合；检验整体结构集合不同及累计位移，而非强求每seed不同。
            CHECK(s.highMidSeeds > 0 && s.unique[1] != s.unique[2] && s.displacementHigh > s.displacementMid,
                  "每声部High与Mid存在同seed差异、结构集合不同，且High累计绝对位移严格大于Mid");
            // Low的回答坐标可由窗口固定；如实打印unique=1，不凭空要求源句随机化。
            CHECK(!s.unique[0].empty() && s.unique[1].size() > 1 && s.unique[2].size() > 1,
                  "每声部Mid/High覆盖多个实际位置结构；Low允许固定窗口回答，unique不计力度差异");
        }
    }

    section("CoreRhythm：整句<=8step恒等与窗口数量无关");
    {
        bool unchanged = true;
        // buildCoreMotif会合并这些短窗；直接构造两窗，只测试voiceRhythm的整句长度早退契约。
        lattice::CoreMotif shortCore;
        shortCore.steps = 8; shortCore.windows = {{0, 4}, {4, 8}};
        shortCore.hits = {{0, 0, 127, 0}, {1, 2, 127, 0}, {0, 4, 127, 1}, {1, 6, 127, 1}};
        for (unsigned grammar = 0; grammar < 4; ++grammar) {
            shortCore.grammar = grammar;
            const auto before = shortCore;
            for (int tier : {0, 1, 2}) {
                const auto voiced = lattice::voiceRhythm(shortCore, tier);
                unchanged &= identity(before, shortCore, true) && identity(before, voiced, true)
                    && safe(voiced, tier);
            }
        }
        CHECK(unchanged, "整句8step即使含多个窗口，四grammar三档仍逐身份逐位置恒等");
    }

    section("CoreRhythm：等长8step窗及12/16/20step单窗，四grammar两member两模式");
    {
        struct Regression { int n, d; std::vector<int> groups; bool narrow; };
        const std::vector<Regression> fixtures {
            {4, 4, {2, 2, 2, 2}, true}, {4, 4, {1, 1, 1, 1, 1, 1, 1, 1}, true},
            {3, 8, {3}, false}, {2, 4, {4}, false}, {5, 8, {5}, false}
        };
        for (const auto& fixture : fixtures) {
            bool coverage = true, accepted = true, conserved = true, bounded = true;
            bool pure = true, integrated = true, repeated = true, helperDifferent = true, barsDifferent = true;
            int cases = 0, helperChanges = 0, barChanges = 0;
            for (bool bb : {false, true}) for (int member : {0, 1}) for (unsigned grammar = 0; grammar < 4; ++grammar) {
                // 选真实generate能产生指定grammar的seed，不用另一套key冒充端到端覆盖。
                int seed = 0;
                while (seed < 16 && (coreKey(seed) + unsigned(member)) % 4u != grammar) ++seed;
                coverage &= seed < 16;
                if (seed == 16) continue;
                lattice::Pattern base; base.meter(fixture.n, fixture.d); base.groups = fixture.groups;
                base.seed = seed; base.backbeat = bb; base.ghost = false; base.accents = true;
                base.fill = 0; base.density = 0; base.development = 0; base.space = 0;
                const auto source = lattice::buildCoreMotif(edges(base), coreKey(seed), member, bb);
                const auto snapshot = source;
                coverage &= source.grammar == grammar && source.steps == base.barTicks() / 60;
                if (fixture.narrow) {
                    coverage &= source.windows.size() == 4;
                    for (const auto& w : source.windows) coverage &= w.end - w.begin == 8;
                } else coverage &= source.windows.size() == 1 && source.steps > 8;
                std::array<lattice::CoreMotif, 3> voiced;
                std::array<lattice::Pattern, 3> generated{base, base, base};
                for (int tier : {0, 1, 2}) {
                    voiced[size_t(tier)] = lattice::voiceRhythm(source, tier);
                    const auto& core = voiced[size_t(tier)];
                    conserved &= identity(source, core) && callSpacing(source, core);
                    bounded &= safe(source) && safe(core, tier);
                    pure &= identity(source, snapshot, true)
                        && identity(core, lattice::voiceRhythm(source, tier), true);
                    if (tier == 0) pure &= identity(source, core, true);
                    auto& p = generated[size_t(tier)]; p.dispersion = tier;
                    accepted &= p.generate(); bounded &= groove_test::safeGridAndLimbs(p);
                    auto again = p; accepted &= again.generate(); pure &= groove_test::sameBars(p, again);
                    for (int b = member; b < lattice::phraseBars; b += 2) integrated &= matches(p, b, core);
                    for (int b = 0; b < lattice::phraseBars; ++b) for (int voice = 0; voice < lattice::tracks; ++voice)
                        repeated &= groove_test::sameLane(p.bars[b][voice], p.bars[b % 2][voice]);
                }
                bool helperChange = false, barChange = false;
                if (fixture.narrow) {
                    if (identity(voiced[1], voiced[2])) {
                        for (size_t i = 0; i < voiced[1].hits.size(); ++i)
                            if (voiced[1].hits[i].voice == 1)
                                helperChange |= voiced[1].hits[i].step != voiced[2].hits[i].step;
                    }
                    barChange = structure(active(generated[1].bars[member][1]))
                        != structure(active(generated[2].bars[member][1]));
                } else {
                    // 问答距离取最后一击Kick到第一击Snare，排除整句统一平移造成的假变化。
                    std::array<int, 3> helperGap{}, barGap{};
                    bool hasVoices = true;
                    for (int tier : {0, 1, 2}) {
                        int lastCall = -1, firstReply = source.steps;
                        for (const auto& h : voiced[size_t(tier)].hits) {
                            if (h.voice == 0) lastCall = std::max(lastCall, h.step);
                            if (h.voice == 1) firstReply = std::min(firstReply, h.step);
                        }
                        hasVoices &= lastCall >= 0 && firstReply < source.steps;
                        helperGap[size_t(tier)] = (firstReply - lastCall) * 60;
                        const auto calls = active(generated[size_t(tier)].bars[member][0]);
                        const auto replies = active(generated[size_t(tier)].bars[member][1]);
                        if (calls.empty() || replies.empty()) { hasVoices = false; continue; }
                        barGap[size_t(tier)] = replies.front().start - calls.back().start;
                        integrated &= helperGap[size_t(tier)] == barGap[size_t(tier)];
                    }
                    helperChange = hasVoices && helperGap[2] > 0 && helperGap[2] != helperGap[0] && helperGap[2] != helperGap[1];
                    barChange = hasVoices && barGap[2] > 0 && barGap[2] != barGap[0] && barGap[2] != barGap[1];
                }
                ++cases; helperChanges += helperChange; barChanges += barChange;
                helperDifferent &= helperChange; barsDifferent &= barChange;
                if (!helperChange || !barChange)
                    std::printf("        退化用例：%d/%d groups=%zu grammar=%u member=%d mode=%d seed=%d helper=%d bars=%d\n",
                                fixture.n, fixture.d, fixture.groups.size(), grammar, member, int(bb), seed,
                                int(helperChange), int(barChange));
            }
            std::printf("        %d/%d groups=%zu：四grammar×两member×两模式=%d；helper变化=%d，真实bars变化=%d\n",
                        fixture.n, fixture.d, fixture.groups.size(), cases, helperChanges, barChanges);
            CHECK(coverage && cases == 16, "每种退化分组完整覆盖四grammar、两member、两模式及预期窗口形状");
            CHECK(accepted && conserved && bounded && pure && integrated && repeated,
                  "退化回归仍保留身份/力度/间距、无碰撞、首Kick边界、确定性、真实bars对照及dev0重复");
            if (fixture.narrow)
                CHECK(helperDifferent && barsDifferent, "2+2+2+2及八个1：每个grammar/member/mode的High与Mid军鼓位置真正不同");
            else
                CHECK(helperDifferent && barsDifferent, "12/16/20step单窗：每个grammar/member/mode的High问答距离同时不同于Low和Mid");
        }
    }

    section("CoreRhythm：128种4/4有序分组与常用奇数拍分组边界");
    bool accepted = true, windows = true, conserved = true, integrated = true, repeat = true, pure = true;
    int configurations = 0;
    auto check = [&](lattice::Pattern base) {
        base.ghost = false; base.fill = 0; base.density = 0; base.development = 0; base.space = 0;
        std::array<std::set<unsigned>, 2> grammars;
        for (int seed = 0; seed < 8; ++seed) for (bool bb : {false, true}) {
            base.seed = seed; base.backbeat = bb;
            for (int tier : {0, 1, 2}) {
                auto p = base; p.dispersion = tier;
                accepted &= p.generate(); ++configurations;
                windows &= groove_test::safeGridAndLimbs(p);
                for (int member : {0, 1}) {
                    const auto source = lattice::buildCoreMotif(edges(p), coreKey(seed), member, bb);
                    grammars[size_t(bb)].insert(source.grammar);
                    const auto before = source;
                    const auto voiced = lattice::voiceRhythm(source, tier);
                    windows &= safe(source) && safe(voiced, tier);
                    conserved &= identity(source, voiced) && callSpacing(source, voiced);
                    pure &= identity(source, before, true)
                        && identity(voiced, lattice::voiceRhythm(source, tier), true);
                    if (tier == 0 || source.steps <= 8) pure &= identity(source, voiced, true);
                    for (int b = member; b < lattice::phraseBars; b += 2) integrated &= matches(p, b, voiced);
                }
                for (int b = 0; b < lattice::phraseBars; ++b) for (int voice = 0; voice < lattice::tracks; ++voice)
                    repeat &= groove_test::sameLane(p.bars[b][voice], p.bars[b % 2][voice]);
            }
        }
        for (const auto& mode : grammars)
            CHECK(mode == std::set<unsigned>({0u, 1u, 2u, 3u}),
                  "每种边界分组、每模式的seed0..7合并两member覆盖全部四grammar（含grammar1）");
    };
    for (int mask = 0; mask < 128; ++mask) {
        lattice::Pattern p; p.groups.clear(); int length = 1;
        for (int gap = 0; gap < 7; ++gap) {
            if (mask & (1 << gap)) { p.groups.push_back(length); length = 1; }
            else ++length;
        }
        p.groups.push_back(length); check(p);
    }
    struct Fixture { int n, d; std::vector<int> groups; };
    const std::vector<Fixture> odd {
        {5, 8, {2, 3}}, {5, 8, {3, 2}}, {7, 8, {2, 2, 3}}, {7, 8, {2, 3, 2}}, {7, 8, {3, 2, 2}},
        {9, 8, {3, 3, 3}}, {9, 8, {2, 2, 2, 3}}, {11, 8, {3, 3, 3, 2}}, {11, 8, {2, 3, 3, 3}},
        {13, 8, {3, 3, 3, 2, 2}}, {15, 8, {3, 3, 3, 3, 3}},
        {5, 4, {3, 3, 2, 2}}, {7, 4, {3, 3, 3, 3, 2}},
        // 最短单窗口、短组并窗及长组拆窗也必须安全，不强求小窗口一定有位移。
        {1, 8, {1}}, {2, 8, {2}}, {2, 8, {1, 1}},
        {3, 8, {1, 1, 1}}, {5, 8, {5}}, {7, 8, {7}}
    };
    for (const auto& fixture : odd) {
        lattice::Pattern p; p.meter(fixture.n, fixture.d); p.groups = fixture.groups; check(p);
    }
    CHECK(configurations == (128 + int(odd.size())) * 48 && accepted,
          "全部128种4/4分组及奇数/极短/长窗口用例覆盖seed0..7、两模式、三档");
    CHECK(windows && conserved && pure, "合并/拆分窗口连续覆盖小节，逐身份守恒、无同轨碰撞、首Kick有界，仅整句<=8step保证恒等");
    CHECK(integrated && repeat, "边界分组真实bars完整保留两声部主干且dev0八轨精确重复");
    std::printf("        边界配置=%d；统计仅描述位置结构与位移，不评价听感。\n", configurations);
}
