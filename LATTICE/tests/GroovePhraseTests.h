#pragma once
#include "../src/Model.h"
#include <cstdio>
#include <limits>
#include <set>
#include <string>
#include <functional>

// 只依赖 CHECK/section；不继承旧 Group carrier、固定 backbeat 或 Kick/Snare 互斥断言。
namespace groove_test {
inline bool sameLane(const lattice::Lane& a, const lattice::Lane& b)
{
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i)
        if (a[i].start != b[i].start || a[i].length != b[i].length
            || a[i].velocity != b[i].velocity) return false;
    return true;
}
inline bool sameBars(const lattice::Pattern& a, const lattice::Pattern& b)
{
    for (int bar = 0; bar < lattice::phraseBars; ++bar)
        for (int r = 0; r < lattice::tracks; ++r)
            if (!sameLane(a.bars[bar][r], b.bars[bar][r])) return false;
    return true;
}
inline int velocity(const lattice::Pattern& p, int b, int r, int tick)
{
    for (const auto& c : p.bars[b][r]) if (c.start == tick) return c.velocity;
    return 0;
}
inline int hits(const lattice::Lane& lane)
{
    return int(std::count_if(lane.begin(), lane.end(), [](const lattice::Cell& c) { return c.velocity > 0; }));
}
inline bool safeGridAndLimbs(const lattice::Pattern& p)
{
    for (int b = 0; b < lattice::phraseBars; ++b) {
        for (int r = 0; r < lattice::tracks; ++r) {
            int end = 0;
            for (const auto& c : p.bars[b][r]) {
                if (c.start != end || c.length != 60 || c.start < 0
                    || c.start + c.length > p.barTicks()
                    || (c.velocity != 0 && c.velocity != 40 && c.velocity != 80 && c.velocity != 127)) return false;
                end += c.length;
            }
            if (end != p.barTicks()) return false;
        }
        for (int t = 0; t < p.barTicks(); t += 60) {
            int hands = 0, cymbals = 0;
            for (int r : {1, 2, 3, 4, 5, 6, 7}) hands += velocity(p, b, r, t) > 0;
            for (int r : {3, 4, 6, 7}) cymbals += velocity(p, b, r, t) > 0;
            if (hands > 2 || cymbals > 1) return false;
            if (velocity(p, b, 4, t) > 0) {
                // 开镲必须由同小节后续闭镲收束，不能靠下一小节或新造闭镲来掩盖悬空。
                if (t + 240 >= p.barTicks() || velocity(p, b, 3, t + 240) <= 0) return false;
            }
        }
    }
    return true;
}
inline std::string rhythm(const lattice::Pattern& p)
{
    std::string key;
    for (const auto& bar : p.bars) for (const auto& lane : bar)
        for (const auto& c : lane) key += c.velocity > 0 ? '1' : '0';
    return key;
}
}

inline void runGroovePhraseTests()
{
    using namespace groove_test;
    section("Groove：64 seeds × 两模式 × 三档发展 × 三档离散度");
    int cases = 0, openHits = 0, ghostHits = 0, accentHits = 0, fillSnareAdditions = 0;
    for (bool bb : {false, true}) for (float dev : {0.f, .45f, .8f}) for (int tier : {0, 1, 2}) {
        bool accepted = true, core = true, repeat = true, deterministic = true;
        bool ghostStable = true, accentsStable = true, densityStable = true, fillStable = true, playable = true;
        for (int seed = 0; seed < 64; ++seed) {
            lattice::Pattern p;
            p.seed = seed; p.backbeat = bb; p.development = dev; p.dispersion = tier;
            accepted &= p.generate(); ++cases;
            auto again = p; accepted &= again.generate(); deterministic &= sameBars(p, again);
            auto dry = p; dry.ghost = false; accepted &= dry.generate();
            auto flat = p; flat.accents = false; accepted &= flat.generate();
            auto sparse = p; sparse.density = 0; accepted &= sparse.generate();
            auto dense = p; dense.density = 1; accepted &= dense.generate();
            auto noFill = p; noFill.fill = 0; accepted &= noFill.generate();
            auto fullFill = p; fullFill.fill = 1; accepted &= fullFill.generate();
            playable &= safeGridAndLimbs(p) && safeGridAndLimbs(dry) && safeGridAndLimbs(flat)
                && safeGridAndLimbs(sparse) && safeGridAndLimbs(dense)
                && safeGridAndLimbs(noFill) && safeGridAndLimbs(fullFill);
            for (int b = 0; b < lattice::phraseBars; ++b) {
                // 不复制生成器的片段抽签，只要求整句核心和 Repeat 小节的击打下限。
                core &= hits(p.bars[b][0]) > 0 && std::any_of(p.bars[b][1].begin(), p.bars[b][1].end(),
                    [](const lattice::Cell& c) { return c.velocity >= 80; });
                if (dev == 0 || b == 0 || b == 1 || b == 2 || b == 3 || b == 4 || b == 8 || b == 9 || b == 12)
                    core &= hits(p.bars[b][0]) >= 3;
                for (int r = 0; r < lattice::tracks; ++r) {
                    if (dev == 0) repeat &= sameLane(p.bars[b][r], p.bars[b % 2][r]);
                    const auto& lane = p.bars[b][r];
                    ghostStable &= lane.size() == dry.bars[b][r].size();
                    accentsStable &= lane.size() == flat.bars[b][r].size();
                    for (const auto& c : lane) {
                        const auto& dl = dry.bars[b][r];
                        const auto& fl = flat.bars[b][r];
                        const auto d = std::find_if(dl.begin(), dl.end(), [&](const lattice::Cell& x) { return x.start == c.start; });
                        const auto f = std::find_if(fl.begin(), fl.end(), [&](const lattice::Cell& x) { return x.start == c.start; });
                        ghostStable &= d != dl.end() && d->length == c.length && d->velocity == (c.velocity == 40 ? 0 : c.velocity);
                        accentsStable &= f != fl.end() && f->length == c.length && f->velocity == (c.velocity == 127 ? 80 : c.velocity);
                        ghostHits += c.velocity == 40; accentHits += c.velocity == 127;
                        openHits += r == 4 && c.velocity > 0;
                    }
                }
                for (const auto& c : sparse.bars[b][0]) if (c.velocity > 0)
                    densityStable &= velocity(p, b, 0, c.start) >= c.velocity;
                for (const auto& c : p.bars[b][0]) if (c.velocity > 0)
                    densityStable &= velocity(dense, b, 0, c.start) >= c.velocity;
                // 对比实际生成的整条 Kick：不预设坐标，也不允许 Fill 新增脚部击打。
                fillStable &= sameLane(noFill.bars[b][0], fullFill.bars[b][0]);
                for (const auto& c : noFill.bars[b][1]) if (c.velocity >= 80)
                    fillStable &= velocity(fullFill, b, 1, c.start) == c.velocity;
                // Close 可在过门空位新增军鼓，但不能覆盖主击或越过停前落点。
                const int rest = fullFill.phraseRestSteps(b);
                const int landing = rest > 0 ? fullFill.barTicks() - (rest + 1) * 60
                                             : fullFill.barTicks() - 240;
                for (const auto& c : fullFill.bars[b][1])
                    if (c.velocity >= 80 && velocity(noFill, b, 1, c.start) < 80) {
                        ++fillSnareAdditions;
                        fillStable &= dev > 0 && b % 8 == 7 && c.start >= landing - 480
                            && c.start < landing && (landing - c.start) % 120 == 0;
                    }
            }
        }
        std::printf("        backbeat=%d dev=%.2f dispersion=%d：64 seeds\n", int(bb), double(dev), tier);
        CHECK(accepted && deterministic, "合法生成成功，同 seed 逐 cell 确定");
        CHECK(core, "每小节 Kick/Snare 非空，4/4 Repeat 小节 Kick 至少三击");
        CHECK(repeat, "dev=0 的八轨在全部16小节精确重复两小节，包括 Crash");
        CHECK(ghostStable, "Ghost 关闭只移除40力度，核心及所有非40 cell 不变");
        CHECK(accentsStable, "Accents 关闭只将127改为80，不改位置、长度、ghost或休止");
        CHECK(densityStable, "Density 从0到1不削减或移动已有 Kick");
        CHECK(fillStable, "Fill从0到1整条实际Kick完全守恒、全部军鼓主击保留，仅Close过门窗口可新增军鼓");
        CHECK(playable, "全部对照无锁手部<=2、镲互斥、Open同小节闭合、网格安全");
    }
    CHECK(openHits > 0 && ghostHits > 0 && accentHits > 0, "Open/Ghost/Accent 比较具有实际覆盖而非空集通过");
    CHECK(fillSnareAdditions > 0, "Fill军鼓新增例外实际触发，不以空集通过");
    std::printf("        核心配置=%d；每配置8次显式generate；Close新增军鼓=%d\n", cases, fillSnareAdditions);

    section("Groove：Space仅第8/16小节禁新触发，停前合击与下一小节回归");
    for (float space : {0.f, .05f, .35f, .75f, 1.f}) {
        bool accepted = true, rests = true, landing = true, kickoff = true;
        int restCases = 0;
        for (int seed = 0; seed < 64; ++seed) for (bool bb : {false, true}) {
            lattice::Pattern p; p.seed = seed; p.backbeat = bb; p.space = space; p.fill = 1; p.development = .8f;
            accepted &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b) {
                const int rest = p.phraseRestSteps(b);
                if (b % 8 != 7 || space == 0) { rests &= rest == 0; continue; }
                ++restCases;
                rests &= rest > 0 && rest <= 8 && rest <= p.barTicks() / 60 / 4;
                const int stop = p.barTicks() - rest * 60;
                landing &= velocity(p, b, 0, stop - 60) >= 80 && velocity(p, b, 1, stop - 60) >= 80;
                for (const auto& lane : p.bars[b]) for (const auto& c : lane)
                    if (c.start >= stop) rests &= c.velocity == 0;
                const auto& nextKick = p.bars[(b + 1) % lattice::phraseBars][0];
                const auto first = std::find_if(nextKick.begin(), nextKick.end(),
                    [](const lattice::Cell& c) { return c.velocity >= 80; });
                kickoff &= first != nextKick.end() && first->start >= 0 && first->start <= 240;
            }
        }
        CHECK(accepted && rests && landing && kickoff, "Space休止无新触发、停前Kick/Snare落点，bar9/循环bar1首Kick有界于0..240tick");
        CHECK(space == 0 || restCases == 256, "每个正Space档覆盖128配置的两个收句小节");
    }

    section("Groove：全部32拍号、负seed和整数边界");
    bool safe = true, repeat = true, deterministic = true;
    int meterCases = 0;
    for (int n = 1; n <= 16; ++n) for (int d : {4, 8})
        for (int seed : {std::numeric_limits<int>::min(), -1729, -1, 0, std::numeric_limits<int>::max()})
            for (float dev : {0.f, .8f}) for (int tier : {0, 1, 2}) {
                lattice::Pattern p; p.meter(n, d); p.seed = seed; p.development = dev;
                p.dispersion = tier; p.fill = 1; p.space = 1; p.density = 1; p.bpm = 180;
                safe &= p.generate() && safeGridAndLimbs(p); ++meterCases;
                auto q = p; safe &= q.generate(); deterministic &= sameBars(p, q);
                for (int b = 0; b < lattice::phraseBars; ++b) {
                    safe &= hits(p.bars[b][0]) > 0 && hits(p.bars[b][1]) > 0;
                    const int rest = p.phraseRestSteps(b), stop = p.barTicks() - rest * 60;
                    if (rest > 0) {
                        safe &= velocity(p, b, 0, stop - 60) >= 80 && velocity(p, b, 1, stop - 60) >= 80;
                        for (const auto& lane : p.bars[b]) for (const auto& c : lane)
                            if (c.start >= stop) safe &= c.velocity == 0;
                    }
                    if (dev == 0) for (int r = 0; r < lattice::tracks; ++r)
                        repeat &= sameLane(p.bars[b][r], p.bars[b % 2][r]);
                }
            }
    CHECK(safe && deterministic && repeat, "32拍号 × 5边界seed × 2发展 × 3离散度：合法安全、可重现、dev0精确重复");
    std::printf("        拍号边界配置=%d\n", meterCases);

    section("Groove：非法参数事务拒绝，不改既有cells或锁");
    using Mutation = std::function<void(lattice::Pattern&)>;
    std::vector<Mutation> invalid {
        [](auto& p) { p.numerator = 0; }, [](auto& p) { p.numerator = 17; },
        [](auto& p) { p.numerator = std::numeric_limits<int>::max(); },
        [](auto& p) { p.denominator = 0; }, [](auto& p) { p.denominator = -4; }, [](auto& p) { p.denominator = 3; },
        [](auto& p) { p.dispersion = -1; }, [](auto& p) { p.dispersion = 3; },
        [](auto& p) { p.groups = {}; }, [](auto& p) { p.groups = {7}; },
        [](auto& p) { p.groups = {0, 8}; }, [](auto& p) { p.groups = {-1, 9}; },
        [](auto& p) { p.groups = {std::numeric_limits<int>::max(), std::numeric_limits<int>::max(), 10}; }
    };
    for (auto member : {&lattice::Pattern::density, &lattice::Pattern::development, &lattice::Pattern::fill, &lattice::Pattern::space})
        for (float value : {-1.f, 1.01f, std::numeric_limits<float>::quiet_NaN(),
                            std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity()})
            invalid.push_back([=](auto& p) { p.*member = value; });
    for (double value : {0., -1., std::numeric_limits<double>::quiet_NaN(),
                         std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity()})
        invalid.push_back([=](auto& p) { p.bpm = value; });
    for (size_t i = 0; i < invalid.size(); ++i) {
        lattice::Pattern p;
        p.subdivide(0, 0, 0, 3, 3); p.bars[0][0][1].velocity = 53;
        p.locked[5] = true; p.barLocked[7] = true;
        invalid[i](p); const auto before = p;
        const auto sameNumber = [](auto a, auto b) { return a == b || (std::isnan(a) && std::isnan(b)); };
        const bool rejected = !p.generate();
        const bool config = p.numerator == before.numerator && p.denominator == before.denominator
            && p.seed == before.seed && p.dispersion == before.dispersion && p.groups == before.groups
            && p.locked == before.locked && p.barLocked == before.barLocked && p.gridStep == before.gridStep
            && p.ghost == before.ghost && p.accents == before.accents && p.backbeat == before.backbeat
            && sameNumber(p.bpm, before.bpm) && sameNumber(p.density, before.density)
            && sameNumber(p.development, before.development) && sameNumber(p.fill, before.fill) && sameNumber(p.space, before.space);
        if (!rejected || !config || !sameBars(p, before)) std::printf("        非法用例=%zu\n", i);
        CHECK(rejected && config && sameBars(p, before), "非法generate事务拒绝，参数、锁与非标准力度/tuplet均不变");
    }

    section("Groove：8轨 × 16小节锁组合保留手工tuplet");
    bool locks = true, changed = false;
    for (int r = 0; r < lattice::tracks; ++r) for (int b = 0; b < lattice::phraseBars; ++b)
        for (float dev : {0.f, .8f}) {
            lattice::Pattern p; p.seed = -31; p.development = dev; p.fill = 1; p.space = 1;
            for (auto& bar : p.bars) for (auto& lane : bar) {
                lane.clear();
                for (int t = 0; t < p.barTicks(); t += 40) lane.push_back({t, 40, t % 120 == 40 ? 53 : 0});
            }
            p.locked[r] = true; p.barLocked[b] = true;
            const auto before = p; locks &= p.generate();
            for (int bar = 0; bar < lattice::phraseBars; ++bar) for (int row = 0; row < lattice::tracks; ++row) {
                if (row == r || bar == b) locks &= sameLane(p.bars[bar][row], before.bars[bar][row]);
                else changed |= !sameLane(p.bars[bar][row], before.bars[bar][row]);
            }
        }
    for (bool trackLocks : {false, true}) {
        lattice::Pattern p; p.subdivide(15, 2, 0, 3, 3);
        p.locked.fill(trackLocks); p.barLocked.fill(!trackLocks);
        const auto before = p; locks &= p.generate() && sameBars(p, before);
    }
    CHECK(locks && changed, "256局部锁配置及两种全锁保留逐cell内容；解锁区域确实重新生成");

    section("Groove：Group与seed确实改变节奏，不仅更换重音");
    std::set<std::string> seeds;
    int groupDifferences = 0;
    for (int seed = 0; seed < 64; ++seed) {
        lattice::Pattern a; a.seed = seed; a.groups = {5, 3};
        lattice::Pattern b = a; b.groups = {3, 5};
        const bool accepted = a.generate() && b.generate();
        CHECK(accepted, "两种合法Group生成成功");
        seeds.insert(rhythm(a)); groupDifferences += rhythm(a) != rhythm(b);
    }
    CHECK(seeds.size() > 1 && groupDifferences > 0, "64seed产生不同节奏，5+3与3+5同seed存在节奏差异");
    std::printf("        不同seed节奏=%zu，同seed Group差异=%d/64；非法事务=%zu\n", seeds.size(), groupDifferences, invalid.size());

    section("Groove回归：关闭重音后按实际问答窗口及Group对照验收");
    struct GroupCase { int n, d; std::vector<int> groups, windowEdges; };
    for (const auto& fixture : std::vector<GroupCase>{{4, 4, {3, 3, 2}, {0, 12, 24, 32}},
                                                     {6, 8, {3, 3}, {0, 12, 24}},
                                                     {7, 8, {3, 2, 2}, {0, 12, 20, 28}},
                                                     {7, 8, {2, 2, 3}, {0, 8, 16, 28}}}) {
        for (bool bb : {false, true}) for (int tier : {0, 1, 2}) {
            bool accepted = true, gridSafe = true, windowShape = true, callsPresent = true, repliesSafe = true;
            int microReplies = 0;
            std::array<int, 2> differences{};
            for (int seed = 0; seed < 32; ++seed) {
                lattice::Pattern p; p.meter(fixture.n, fixture.d); p.groups = fixture.groups;
                p.seed = seed; p.accents = false; p.ghost = false; p.development = 0;
                p.fill = 0; p.density = 0; p.backbeat = bb; p.dispersion = tier;
                const bool generated = p.generate(); accepted &= generated;
                gridSafe &= safeGridAndLimbs(p);
                auto ungrouped = p; ungrouped.groups = {p.eighths()};
                const bool ungroupedGenerated = ungrouped.generate(); accepted &= ungroupedGenerated;
                gridSafe &= safeGridAndLimbs(ungrouped);
                std::vector<int> edges{0};
                for (int group : p.groups) edges.push_back(edges.back() + group * 4);
                const auto core = lattice::buildCoreMotif(edges, 0u, 0, bb);
                windowShape &= core.windows.size() + 1 == fixture.windowEdges.size();
                for (size_t i = 0; i < core.windows.size(); ++i) {
                    const auto& w = core.windows[i];
                    if (i + 1 >= fixture.windowEdges.size()) { windowShape = false; continue; }
                    windowShape &= w.begin == fixture.windowEdges[i] && w.end == fixture.windowEdges[i + 1];
                    for (int b : {0, 1}) {
                        // Kick搜索域不变。只有整句>8step的High窄窗允许1step回答微位置；
                        // Low/Mid、宽窗及整句<=8仍保留2step边界，长单窗也不扩大边界。
                        bool call = false;
                        const int lower = i == 0 ? 0 : std::max(0, w.begin - 4);
                        for (int s = lower; s <= w.end - 6; ++s)
                            call |= velocity(p, b, 0, s * 60) == 80;
                        const bool micro = tier == 2 && core.steps > 8 && w.end - w.begin <= 8;
                        const int margin = micro ? 1 : 2;
                        int replyCount = 0, replyStep = -1;
                        bool replyBounds = true;
                        // 检查整个半开窗口，避免“范围内有一击”掩盖额外或越界的Snare。
                        for (int s = w.begin; s < w.end; ++s) {
                            const int v = velocity(p, b, 1, s * 60);
                            if (v <= 0) continue;
                            ++replyCount; replyStep = s;
                            replyBounds &= v == 80 && s >= w.begin + margin && s <= w.end - margin;
                            microReplies += micro && (s == w.begin + 1 || s == w.end - 1);
                        }
                        const bool reply = replyCount == 1 && replyBounds;
                        callsPresent &= call; repliesSafe &= reply;
                        if (!call || !reply)
                            std::printf("        Group条件失败 %d/%d groups=%zu seed=%d mode=%d tier=%d member=%d window=%zu [%d,%d)："
                                        "Kick=%d Snare数量=%d 位置=%d 合法范围=[%d,%d]\n",
                                        fixture.n, fixture.d, fixture.groups.size(), seed, int(bb), tier, b, i,
                                        w.begin, w.end, int(call), replyCount, replyStep, w.begin + margin, w.end - margin);
                    }
                }
                for (int voice : {0, 1})
                    differences[voice] += !sameLane(p.bars[0][voice], ungrouped.bars[0][voice])
                        || !sameLane(p.bars[1][voice], ungrouped.bars[1][voice]);
            }
            std::printf("        Group窗口 %d/%d groups=", fixture.n, fixture.d);
            for (size_t i = 0; i < fixture.groups.size(); ++i)
                std::printf("%s%d", i == 0 ? "" : "+", fixture.groups[i]);
            std::printf(" mode=%d tier=%d：Kick差异=%d/32 Snare差异=%d/32 High窄窗边缘回答=%d；"
                        "生成=%d 网格=%d 窗口=%d Kick=%d Snare=%d\n",
                        int(bb), tier, differences[0], differences[1], microReplies,
                        int(accepted), int(gridSafe), int(windowShape), int(callsPresent), int(repliesSafe));
            CHECK(accepted && gridSafe, "Group与单组对照生成成功、网格及肢体安全");
            CHECK(windowShape, "实际Group窗口数量及边界与fixture精确一致");
            CHECK(callsPresent, "三档真实bars每窗口仍有原合法范围内的Kick问句");
            CHECK(repliesSafe, "每窗口恰好一个80力度Snare，仅High窄窗允许begin+1/end-1，其余保留2step边界");
            bool hasNarrow = false;
            for (size_t i = 1; i < fixture.windowEdges.size(); ++i)
                hasNarrow |= fixture.windowEdges[i] - fixture.windowEdges[i - 1] <= 8;
            CHECK(tier != 2 || !hasNarrow || microReplies > 0,
                  "含窄窗的High配置实际覆盖边缘回答，拒绝仅扩大范围却无回归覆盖");
            CHECK(differences[0] > 16 && differences[1] > 16,
                  "关闭Accent/Ghost/Fill/Density/Development后，Group对两声部各自在多数seed产生位置差异");
        }
    }
    {
        // 只比较源动机的声部方向，不比较位置或高低数量；Group可以改变可用窗口。
        auto tomClass = [](const lattice::Pattern& p) {
            std::string voices;
            for (int t = 0; t < p.barTicks(); t += 60) {
                if (velocity(p, 1, 5, t) > 0) voices += 'H';
                if (velocity(p, 1, 2, t) > 0) voices += 'L';
            }
            if (voices.empty()) return '-';
            if (voices.find('L') == std::string::npos) return 'H';
            if (voices.find('H') == std::string::npos) return 'L';
            return voices.front() == 'H' ? 'D' : 'U';
        };
        bool stable = true; int active = 0;
        for (int seed = 0; seed < 32; ++seed) {
            lattice::Pattern a; a.seed = seed; a.development = 0; a.ghost = false;
            auto b = a; b.groups = {5, 3};
            stable &= a.generate() && b.generate();
            const char ca = tomClass(a), cb = tomClass(b);
            stable &= ca == cb; active += ca != '-' && cb != '-';
        }
        CHECK(stable && active >= 8, "同拍号改Group不重抽源Tom高/低/上下行分类，至少8个实际有Tom的seed");
    }

    section("Groove回归：锁已有Open后发展仍闭合，不新增第三只手");
    {
        bool accepted = true, kept = true, closed = true, handsSafe = true;
        int activeSeeds = 0, checkedOpens = 0, pushOpens = 0;
        for (int seed = 0; seed < 32; ++seed) {
            lattice::Pattern p; p.seed = seed; p.development = 0;
            accepted &= p.generate();
            int opens = 0; for (const auto& bar : p.bars) opens += hits(bar[4]);
            if (opens == 0) continue;
            ++activeSeeds; const auto before = p;
            p.locked[4] = true; p.development = .45f; accepted &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b) {
                kept &= sameLane(p.bars[b][4], before.bars[b][4]);
                const int stop = p.barTicks() - p.phraseRestSteps(b) * 60;
                for (const auto& open : p.bars[b][4]) if (open.velocity > 0) {
                    ++checkedOpens; pushOpens += b == 10 || b == 14;
                    int end = stop;
                    for (const auto& next : p.bars[b][4])
                        if (next.velocity > 0 && next.start > open.start) end = std::min(end, next.start);
                    bool hat = false;
                    for (const auto& c : p.bars[b][3])
                        hat |= c.velocity > 0 && c.start > open.start && c.start < end;
                    closed &= hat;
                }
                // 锁Open可在不同合法位置闭合，不套用无锁生成的固定240tick断言。
                for (int t = 0; t < p.barTicks(); t += 60) {
                    int hands = 0;
                    for (int r : {1, 2, 3, 4, 5, 6, 7}) hands += velocity(p, b, r, t) > 0;
                    handsSafe &= hands <= 2;
                }
            }
        }
        std::printf("        锁Open：有效seeds=%d/32，Open=%d，Push Open=%d\n", activeSeeds, checkedOpens, pushOpens);
        CHECK(accepted && activeSeeds >= 8 && checkedOpens > 0 && pushOpens > 0,
              "锁Open回归覆盖多个真实有Open的seed以及Ride Push小节，拒绝空集通过");
        CHECK(kept && closed && handsSafe, "锁Open逐cell不变，休止/下一Open前有后续Hat，所有时刻手部<=2");
    }

    section("Groove回归：锁空轨不产生虚拟Crash或收句合击");
    {
        bool crashSafe = true, closeSafe = true;
        for (int seed = 0; seed < 16; ++seed) {
            lattice::Pattern p; p.clear(); p.seed = seed; p.locked[7] = true;
            const auto before = p; crashSafe &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b)
                crashSafe &= sameLane(p.bars[b][7], before.bars[b][7]);
            for (int b : {0, 8}) crashSafe &= velocity(p, b, 3, 0) >= 80;

            p.clear(); p.locked[0] = p.locked[1] = true;
            p.fill = 0; p.space = 0; p.density = 0; p.ghost = false;
            const auto empty = p; closeSafe &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b) for (int r : {0, 1})
                closeSafe &= sameLane(p.bars[b][r], empty.bars[b][r]);
            // Fill/Space关闭但仍有收句角色，检查最后八分Hat不能被不存在的合击清空。
            for (int b : {7, 15}) closeSafe &= velocity(p, b, 3, p.barTicks() - 240) >= 80;
        }
        CHECK(crashSafe, "16seed锁空Crash逐cell不变，bar1/bar9首拍Hat不被虚拟Crash清除");
        CHECK(closeSafe, "16seed锁空Kick/Snare保持空轨，Fill/Space关闭时收句Hat不被虚拟合击清除");
    }

    section("Groove回归：128种4/4有序分组及全部拍号奇数组安全");
    {
        bool groupedSafe = true; int configurations = 0;
        auto checkGroups = [&](lattice::Pattern& p) {
            groupedSafe &= p.generate() && safeGridAndLimbs(p); ++configurations;
            for (int b = 0; b < lattice::phraseBars; ++b)
                groupedSafe &= hits(p.bars[b][0]) > 0 && hits(p.bars[b][1]) > 0;
        };
        // 八个八分音符间的七个切点，逐一枚举全部2^7种有序分组。
        for (int mask = 0; mask < 128; ++mask) for (int seed : {-1, 7}) {
            lattice::Pattern p; p.groups.clear(); int length = 1;
            for (int gap = 0; gap < 7; ++gap) {
                if (mask & (1 << gap)) { p.groups.push_back(length); length = 1; }
                else ++length;
            }
            p.groups.push_back(length); p.seed = seed; p.fill = 1; p.space = 1;
            checkGroups(p);
        }
        for (int n = 1; n <= 16; ++n) for (int d : {4, 8}) {
            lattice::Pattern p; p.meter(n, d); p.groups.clear();
            int remaining = p.eighths();
            while (remaining >= 3) { p.groups.push_back(3); remaining -= 3; }
            while (remaining-- > 0) p.groups.push_back(1);
            p.seed = -17; p.fill = 1; p.space = 1;
            checkGroups(p);
        }
        CHECK(groupedSafe && configurations == 288,
              "128种4/4分组×2seed及32拍号奇数组：网格、双手、镲闭合安全且每bar Kick/Snare非空");
    }
}
