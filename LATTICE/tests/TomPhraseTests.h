#pragma once
#include "GroovePhraseTests.h"
#include <utility>

// 性质而非旧模板：允许仅高、仅低、上下行及不等数量；不强制每句2..4击或先高后低。
inline void runTomGroovePropertyTests()
{
    using namespace groove_test;
    section("Tom：单支高/低与双向句法，Ghost独立、确定性、dev0重复");
    for (bool bb : {false, true}) for (float dev : {0.f, .45f, .8f}) {
        int highOnly = 0, lowOnly = 0, descending = 0, ascending = 0;
        int highHits = 0, lowHits = 0, activeSeeds = 0;
        bool safe = true, independent = true, deterministic = true, repeated = true;
        for (int seed = 0; seed < 64; ++seed) {
            lattice::Pattern p; p.seed = seed; p.backbeat = bb; p.development = dev; p.dispersion = seed % 3;
            safe &= p.generate();
            auto dry = p; dry.ghost = false; safe &= dry.generate();
            auto again = p; safe &= again.generate();
            int total = 0;
            for (int b = 0; b < lattice::phraseBars; ++b) {
                std::vector<std::pair<int, int>> events;
                const int hi = hits(p.bars[b][5]), lo = hits(p.bars[b][2]);
                highOnly += hi > 0 && lo == 0; lowOnly += lo > 0 && hi == 0;
                highHits += hi; lowHits += lo; total += hi + lo;
                for (int r : {5, 2}) {
                    independent &= sameLane(p.bars[b][r], dry.bars[b][r]);
                    deterministic &= sameLane(p.bars[b][r], again.bars[b][r]);
                    if (dev == 0) repeated &= sameLane(p.bars[b][r], p.bars[b % 2][r]);
                    for (const auto& c : p.bars[b][r]) if (c.velocity > 0) {
                        safe &= (c.velocity == 80 || c.velocity == 127) && c.start >= 0
                            && c.start + c.length <= p.barTicks() && c.start % 60 == 0 && c.length == 60;
                        events.emplace_back(c.start, r);
                    }
                }
                std::sort(events.begin(), events.end());
                for (size_t i = 1; i < events.size(); ++i) if (events[i - 1].first < events[i].first) {
                    descending += events[i - 1].second == 5 && events[i].second == 2;
                    ascending += events[i - 1].second == 2 && events[i].second == 5;
                }
            }
            activeSeeds += total > 0;
        }
        std::printf("        Tom bb=%d dev=%.2f: active=%d/64 high=%d low=%d highOnly=%d lowOnly=%d down=%d up=%d\n",
                    int(bb), double(dev), activeSeeds, highHits, lowHits, highOnly, lowOnly, descending, ascending);
        CHECK(safe && independent && deterministic && repeated, "通鼓网格/力度合法、Ghost独立、同seed可重现、dev0精确重复");
        CHECK(highOnly > 0 && lowOnly > 0 && descending > 0 && ascending > 0,
              "64seed覆盖高音单支、低音单支、下行、上行；不限制高低数量比例");
    }

    section("Tom：Fill/Space/速度矩阵使用实际休止边界");
    bool accepted = true, windows = true, noGhost = true;
    int fillHits = 0;
    for (float fill : {0.f, .05f, .5f, 1.f}) for (float space : {0.f, .35f, 1.f})
        for (double bpm : {100., 180.}) for (int seed = 0; seed < 64; ++seed) {
            lattice::Pattern p; p.seed = seed; p.fill = fill; p.space = space; p.bpm = bpm;
            p.development = .8f; p.ghost = false; p.accents = false;
            accepted &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b) {
                const int stop = p.barTicks() - p.phraseRestSteps(b) * 60;
                for (int r : {2, 5}) for (const auto& c : p.bars[b][r]) if (c.velocity > 0) {
                    windows &= c.start >= 0 && c.start < stop && c.start + c.length <= p.barTicks();
                    noGhost &= c.velocity == 80;
                    fillHits += fill == 1 && b % 8 == 7;
                }
            }
        }
    CHECK(accepted && windows && noGhost && fillHits > 0, "1536配置的通鼓不侵入休止，关闭Accent/Ghost后均为80，存在收句通鼓");

    section("Tom：单轨锁不妨碍另一支，锁小节保留原内容");
    for (int lockedTrack : {2, 5}) {
        bool locks = true; int unlockedHits = 0;
        for (int seed = 0; seed < 64; ++seed) {
            lattice::Pattern p; p.clear(); p.seed = seed; p.development = .8f; p.fill = 1;
            p.locked[lockedTrack] = true;
            for (int b = 0; b < lattice::phraseBars; ++b) {
                auto& lane = p.bars[b][lockedTrack]; lane.clear();
                for (int t = 0; t < p.barTicks(); t += 40) lane.push_back({t, 40, t == 40 ? 53 : 0});
            }
            for (int b : {1, 7, 15}) {
                p.barLocked[b] = true;
                for (int r : {2, 5}) p.bars[b][r] = {{0, 80, 103}, {80, p.barTicks() - 80, 0}};
            }
            const auto before = p; locks &= p.generate();
            for (int b = 0; b < lattice::phraseBars; ++b) {
                locks &= sameLane(p.bars[b][lockedTrack], before.bars[b][lockedTrack]);
                if (p.barLocked[b]) for (int r : {2, 5}) locks &= sameLane(p.bars[b][r], before.bars[b][r]);
                else unlockedHits += hits(p.bars[b][lockedTrack == 2 ? 5 : 2]);
            }
        }
        CHECK(locks && unlockedHits > 0, "锁轨/锁小节逐cell保留tuplet与非标准力度，未锁通鼓仍可独立成句");
    }
}

inline void runTomSourceCompatibilityTests()
{
    using namespace groove_test;
    section("Tom：源句发展不强行引入空通鼓，保留手工三连音与源小节");
    bool sourceKept = true, emptyKept = true, gridKept = true, sourceDev0 = true;
    for (int mask = 0; mask < 4; ++mask) for (float dev : {0.f, .8f})
        for (bool ghost : {false, true}) for (int seed = 0; seed < 12; ++seed) {
            lattice::Pattern p; p.clear(); p.seed = seed; p.development = dev;
            p.ghost = ghost; p.fill = 1; p.space = .8f; p.dispersion = seed % 3;
            p.bars[0][0][0].velocity = 101;
            for (int r : {5, 2}) {
                auto& lane = p.bars[0][r]; lane.clear();
                for (int t = 0; t < p.barTicks(); t += 80)
                    lane.push_back({t, 80, ((mask & (r == 5 ? 1 : 2)) && t == (r == 5 ? 960 : 1280)) ? 113 : 0});
            }
            const auto source = p.bars[0];
            sourceKept &= p.developFromBar1();
            for (int r : {5, 2}) {
                sourceKept &= sameLane(p.bars[0][r], source[r]);
                for (int b = 1; b < lattice::phraseBars; ++b) {
                    const auto& lane = p.bars[b][r];
                    if (hits(source[r]) == 0) emptyKept &= sameLane(lane, source[r]);
                    if (dev == 0) sourceDev0 &= sameLane(lane, source[r]);
                    gridKept &= lane.size() == source[r].size();
                    if (lane.size() == source[r].size()) for (size_t i = 0; i < lane.size(); ++i)
                        gridKept &= lane[i].start == source[r][i].start && lane[i].length == source[r][i].length;
                }
            }
        }
    CHECK(sourceKept && emptyKept, "192配置：源通鼓双空、仅高、仅低、双支保持声部边界，不强配另一支");
    CHECK(gridKept && sourceDev0, "源通鼓手工cell结构不变，dev0精确复制而不套用生成短句");
}

inline void runTomPhraseTests()
{
    runTomGroovePropertyTests();
    runTomSourceCompatibilityTests();
}
