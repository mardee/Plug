#include "../plugin/NightShiftLighting.h"

#include <cstdlib>
#include <iostream>
#include <limits>

using Lighting = glint::NightShiftLighting;
using Grid = glint::NightShiftTimeGrid;
using Level = Lighting::Level;
namespace
{
int checks = 0;
void check (bool ok, const char* expression, int line)
{
    ++checks;
    if (!ok)
    {
        std::cerr << "FAIL line " << line << ": " << expression << '\n';
        std::exit (1);
    }
}
#define CHECK(expression) check ((expression), #expression, __LINE__)
bool near (double a, double b) { return std::abs (a - b) < 0.00001; }
void dark (const Lighting::Sample& s)
{
    CHECK (!s.enabled && !s.echoActive && s.echoCount == 0);
    CHECK (s.steady == 0 && s.idle == 0 && s.flash == 0);
    CHECK (s.welcomeProgress < 0 && s.enableProgress < 0 && s.idleProgress < 0);
    for (int cell = 0; cell < Grid::positionCount; ++cell)
    {
        const auto levels = Lighting::cellLevels (s, cell, true);
        CHECK (levels.edge == Level::off && levels.center == Level::off && !levels.echoActualCell);
        CHECK (Lighting::intensityAt (s, Grid::normalizedX (cell)) == 0);
    }
}
void timeGrid()
{
    CHECK (Grid::stepsPerBeat == 16 && Grid::stepsPerWindow == 64 && Grid::positionCount == 65);
    CHECK (Grid::stepBeats == 0.0625 && Grid::windowBeats == 4);
    const std::array<int, 10> choices {{ 2, 3, 4, 6, 8, 12, 16, 24, 32, 48 }};
    for (int choice = 0; choice < 10; ++choice)
    {
        CHECK (Grid::choiceCell (choice) == choices[static_cast<std::size_t> (choice)]);
        CHECK (Grid::choiceCell (choice) * Grid::stepBeats == ozo::midi::noteBeats[static_cast<std::size_t> (choice)]);
        for (int repeat = 0; repeat < 10; ++repeat)
            for (int page = 0; page < 4; ++page)
                for (int cell = 0; cell < Grid::positionCount; ++cell)
                {
                    const int offset = page * 64 + cell - choices[static_cast<std::size_t> (choice)];
                    const bool expected = offset >= 0 && offset % choices[static_cast<std::size_t> (repeat)] == 0;
                    CHECK (Grid::isTargetCell (choice, repeat, cell, page) == expected);
                    CHECK (Grid::isTarget (cell, choice, repeat, page) == expected);
                }
    }
    for (int cell = 0; cell < Grid::positionCount; ++cell)
    {
        CHECK (Grid::normalizedX (cell) == cell / 64.0f);
        CHECK (Grid::beatForCell (cell) == cell * 0.0625);
        if (cell > 0) CHECK (near (Grid::normalizedX (cell) - Grid::normalizedX (cell - 1), 1.0 / 64));
        CHECK (Grid::cellForBeat (Grid::beatForCell (cell)) == (cell == 64 ? 0 : cell));
    }
    CHECK (Grid::isTargetCell (8, 8, 64)); // First 2 + Repeat 2 = right endpoint.
    CHECK (!Grid::isTargetCell (4, 6, 0));
    CHECK (Grid::isTargetCell (4, 6, 8) && Grid::isTargetCell (4, 6, 56));
    CHECK (!Grid::isTargetCell (4, 6, 12) && !Grid::isTargetCell (4, 6, 64));
    CHECK (Grid::cellForOnset (6.1875) == 35 && Grid::pageForOnset (6.1875) == 1);
    CHECK (Grid::cellForOnset (4) == 0 && Grid::pageForOnset (4) == 1);
    CHECK (Grid::cellForOnset (16.5) == 8 && Grid::pageForOnset (16.5) == 4);
    CHECK (Grid::cellForOnset (0.16) == 3); // Off-grid telemetry rounds to nearest position.
    CHECK (Grid::cellForOnset (3.99) == 64);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK (Grid::cellForOnset (-1) == -1 && Grid::cellForOnset (nan) == -1);
    CHECK (Grid::pageForOnset (nan) == -1 && Grid::choiceCell (-1) == -1 && Grid::choiceCell (10) == -1);
    CHECK (!Grid::isTargetCell (0, 0, -1) && !Grid::isTargetCell (0, 0, 65));
    CHECK (!Grid::isTargetCell (-1, 0, 2) && !Grid::isTargetCell (0, 10, 2));
    CHECK (!Grid::isTargetCell (0, 0, 2, -1) && !Grid::isTargetCell (0, 0, 2, 0.5));
    CHECK (!Grid::isTargetCell (0, 0, 2, nan));
}
void fiveLevelsAndSettings()
{
    CHECK (Lighting::levelValue (Level::off) == 0);
    CHECK (Lighting::levelValue (Level::base) == 0.15f);
    CHECK (Lighting::levelValue (Level::mark) == 0.4f);
    CHECK (Lighting::levelValue (Level::echo) == 0.7f);
    CHECK (Lighting::levelValue (Level::peak) == 1.0f);
    CHECK (Lighting::levelValue (Level::peak) - Lighting::levelValue (Level::base) > 0.8f);
    Lighting l;
    l.start (0, 3, 0);
    const auto ready = l.sample (0, 2000);
    CHECK (Lighting::cellLevel (l.sample (2, 2000), 7) == Level::off);
    CHECK (Lighting::cellLevel (ready, 7) == Level::base);
    CHECK (Lighting::cellLevel (ready, 7, true) == Level::mark);
    CHECK (Lighting::cellLevel (ready, 16) == Level::mark); // Strong beat.
    for (int cell = 0; cell <= 64; ++cell)
    {
        const auto levels = Lighting::cellLevels (ready, cell, 4, 6);
        CHECK (levels.target == Grid::isTargetCell (4, 6, cell));
        CHECK (levels.center == (levels.target ? Level::mark : Level::off));
        CHECK (!levels.echoActualCell && !ready.echoActive);
    }
    CHECK (l.echo (0, 0.79f, 2000, 2000, 0.5));
    CHECK (l.echo (1, 0.8f, 2000, 2000, 0.5));
    CHECK (Lighting::cellLevel (l.sample (0, 2000), 8) == Level::echo);
    CHECK (Lighting::cellLevel (l.sample (1, 2000), 8) == Level::peak);
    CHECK (Lighting::velocityLevel (1.0f / 127) == Level::echo);
    CHECK (Lighting::velocityLevel (0) == Level::off);
    CHECK (Lighting::cellLevel (ready, -1, true) == Level::off);
    CHECK (Lighting::cellLevel (ready, 65, true) == Level::off);
}
void welcome()
{
    Lighting l;
    l.syncEnabled (1, 7, 500); // Hidden/unstarted must not animate.
    l.update (500);
    dark (l.sample (0, 500));
    l.start (1000, 1, 7);
    CHECK (l.welcomeProgress (1000) == 0);
    int previousHead = -1;
    for (int cell = 0; cell <= 64; ++cell)
    {
        const double t = 1000 + (cell + 0.5) / 72.0 * Lighting::welcomeMs;
        for (std::size_t lane = 0; lane < Lighting::laneCount; ++lane)
        {
            const auto levels = Lighting::cellLevels (l.sample (lane, t), cell);
            CHECK (levels.edge == Level::peak); // Complete left-to-right, including initially OFF lanes.
            CHECK (levels.center == Level::off && !levels.echoActualCell);
        }
        int head = -1;
        const auto s = l.sample (3, t);
        for (int x = 0; x <= 64; ++x)
            if (Lighting::cellLevels (s, x).edge == Level::peak) head = x;
        CHECK (head >= previousHead);
        previousHead = head;
    }
    int lit = 0;
    const auto middle = l.sample (3, 1750);
    for (int cell = 0; cell <= 64; ++cell)
        lit += Lighting::cellLevel (middle, cell) != Level::off ? 1 : 0;
    CHECK (lit >= 3 && lit <= 11); // A visible group, never a global flash.
    CHECK (near (l.welcomeProgress (1750), 0.5));
    l.start (1750, 1, 7);
    CHECK (near (l.welcomeProgress (1750), 0.5));
    CHECK (l.welcomeProgress (2499) >= 0 && l.welcomeProgress (2500) < 0);
    dark (l.sample (3, 2500));
    l.start (2600, 1, 7);
    CHECK (l.welcomeProgress (2600) < 0);
}
void offAndEnable()
{
    Lighting l;
    l.start (0, 1, 0);
    CHECK (l.echo (0, 1, 100, 100, 0.5));
    l.syncEnabled (0, 0, 110);
    dark (l.sample (0, 110));
    CHECK (l.sample (1, 110).welcomeProgress > 0 && l.welcomeProgress (110) > 0);
    CHECK (!l.echo (0, 1, 110, 110, 0.5));
    l.syncEnabled (1, 0, 120);
    auto s = l.sample (0, 120);
    CHECK (s.enabled && s.enableProgress == 0 && s.welcomeProgress < 0);
    CHECK (!l.echo (0, 1, 119, 120, 0.5));
    CHECK (l.echo (0, 1, 120, 120, 0.5));
    l.syncEnabled (0, 0, 130);
    dark (l.sample (0, 130));
    l.syncEnabled (16, 0, 2000); // Engine bit four, not display order.
    for (int cell = 0; cell <= 64; ++cell)
    {
        s = l.sample (4, 2000 + (cell + 0.5) / 72.0 * Lighting::enableMs);
        CHECK (Lighting::cellLevels (s, cell).edge == Level::peak);
        CHECK (Lighting::cellLevels (s, cell).center == Level::off && !s.echoActive);
    }
    l.syncEnabled (16, 0, 2400);
    CHECK (l.sample (4, 2700).enableProgress < 0);
    CHECK (near (l.intensityAt (4, 7 / 64.0f, 2700), 0.15f));
    dark (l.sample (0, 6000));
    dark (l.sample (3, 2350));
}
void idle()
{
    for (std::size_t lane = 0; lane < Lighting::laneCount; ++lane)
    {
        Lighting l;
        l.start (100, 127, 0);
        const double first = 4100 + Lighting::lanePhaseMs * lane;
        CHECK (l.sample (lane, first - 0.1).idleProgress < 0);
        CHECK (l.sample (lane, first).idleProgress == 0);
        for (int cycle = 0; cycle < 3; ++cycle)
        {
            const double t = first + cycle * 4000;
            for (int cell : { 0, 16, 32, 48, 64 })
            {
                const auto s = l.sample (lane, t + (cell + 0.5) / 72.0 * Lighting::idleFlashMs);
                const auto levels = Lighting::cellLevels (s, cell);
                CHECK (levels.edge == Level::echo && levels.center == Level::off && !s.echoActive);
            }
            const auto before = l.sample (lane, t - 1), middle = l.sample (lane, t + 275);
            CHECK (Lighting::intensityAt (middle, 36 / 64.0f) - Lighting::intensityAt (before, 36 / 64.0f) > 0.5f);
            CHECK (Lighting::cellLevel (middle, 60) == Level::base);
            CHECK (near (middle.idleProgress, 0.5) && middle.idle == 0.7f);
            CHECK (l.sample (lane, t + 550).idleProgress < 0);
        }
    }
}
void realEchoAndIdleRestart()
{
    Lighting l;
    l.start (0, 1, 0);
    for (int t = 0; t <= 4000; t += 17) CHECK (!l.sample (0, t).echoActive);
    l.update (4200);
    CHECK (l.echo (0, 0.6f, 4100, 4200, 6.5)); // Late delivery retains 100 ms age, page one.
    auto s = l.sample (0);
    CHECK (s.echoActive && s.echoCount == 1 && s.idleProgress < 0);
    CHECK (near (s.echoes[0].progress, 100.0 / Lighting::echoMs) && s.echoes[0].cell == 40 && s.echoes[0].step == 40);
    CHECK (near (s.echoes[0].sweepProgress, 100.0 / 420)); // Head keeps the old speed, independent of afterglow.
    CHECK (s.echoes[0].page == 1 && s.echoes[0].onsetBeats == 6.5 && s.echoes[0].beatPosition == 6.5);
    auto actual = Lighting::cellLevels (s, 40, 4, 6);
    CHECK (actual.echoActualCell && actual.center == Level::echo);
    CHECK (Lighting::cellLevels (s, 17).edge == Level::echo); // Easy-to-see moving edge head.
    CHECK (Lighting::cellLevels (s, 17).center == Level::off && !Lighting::cellLevels (s, 17).echoActualCell);
    CHECK (Lighting::cellLevels (l.sample (0, 4259), 40).center == Level::echo);
    CHECK (Lighting::cellLevels (l.sample (0, 4261), 40).center == Level::mark);
    l.update (4300);
    CHECK (l.echo (0, 1, 4300, 4300, 0.75)); // Independent cloud, not a new global phase.
    s = l.sample (0, 4300);
    CHECK (s.echoCount == 2 && s.flash == 1);
    CHECK (Lighting::cellLevels (s, 12).center == Level::peak);
    s = l.sample (0, 4510);
    CHECK (Lighting::cellLevels (s, 40).echoActualCell && Lighting::cellLevels (s, 12).echoActualCell);
    CHECK (Lighting::cellLevels (s, 12).center == Level::echo);
    CHECK (Lighting::cellLevels (s, 36).edge == Level::peak);
    s = l.sample (0, 4720); // 420 ms: the head has finished, not the actual-cell afterglow.
    CHECK (s.echoActive && s.echoes[1].sweepProgress < 0 && s.echoes[1].cell == 12);
    CHECK (Lighting::cellLevels (s, 12).echoActualCell && Lighting::cellLevels (s, 12).center == Level::echo);
    CHECK (Lighting::cellLevels (s, 36).edge == Level::base);
    CHECK (Lighting::cellLevels (l.sample (0, 4801), 12).center == Level::mark);
    const double lastEnd = 4300 + Lighting::echoMs;
    CHECK (Lighting::cellLevels (l.sample (0, lastEnd - 1), 12).center == Level::mark);
    CHECK (!l.sample (0, lastEnd).echoActive);
    CHECK (Lighting::cellLevels (l.sample (0, lastEnd), 12).center == Level::off);
    CHECK (l.sample (0, lastEnd + 3999).idleProgress < 0);
    CHECK (near (l.sample (0, lastEnd + 4275).idleProgress, 0.5)); // Last EVENT end + 4s + half sweep.
    Lighting legacy;
    legacy.start (0, 1, 0);
    CHECK (legacy.echo (0, 1, 5000, 5250));
    s = legacy.sample (0, 5250);
    CHECK (s.echoActive && s.echoes[0].cell == -1 && s.echoes[0].page == -1);
    for (int cell = 0; cell <= 64; ++cell) CHECK (!Lighting::cellLevels (s, cell).echoActualCell);
    CHECK (near (legacy.sample (0, 5000 + Lighting::echoMs + 4275).idleProgress, 0.5)); // Receipt does not restart idle.
}
void overlapAndBounds()
{
    Lighting l;
    l.start (0, 1, 0);
    CHECK (l.echo (0, 1, 2000, 2000, 4));
    CHECK (l.echo (0, 1, 2010, 2010, 8.125));
    CHECK (l.echo (0, 1, 2020, 2020, 3.9375));
    auto s = l.sample (0, 2020);
    CHECK (Lighting::cellLevels (s, 0).echoActualCell);
    CHECK (Lighting::cellLevels (s, 2).echoActualCell);
    CHECK (Lighting::cellLevels (s, 63).echoActualCell);
    CHECK (!l.echo (0, 1, 1990, 2020, 0.5)); // Older fourth cannot evict a new pulse.
    CHECK (l.echo (0, 4, 2030, 2030, 0.75));
    s = l.sample (0, 2030);
    CHECK (s.echoCount == Lighting::echoCapacity && !Lighting::cellLevels (s, 0).echoActualCell);
    for (std::size_t i = 0; i < s.echoCount; ++i) CHECK (s.echoes[i].strength == 1);
    for (int t = 2030; t <= 2030 + Lighting::echoMs; t += 5)
        for (int cell = 0; cell <= 64; ++cell)
        {
            const float value = Lighting::intensityAt (l.sample (0, t), Grid::normalizedX (cell));
            CHECK (value == 0 || value == 0.15f || value == 0.4f || value == 0.7f || value == 1);
        }
    CHECK (!l.sample (0, 2030 + Lighting::echoMs).echoActive);
    CHECK (Lighting::intensityAt (s, -1) == Lighting::intensityAt (s, 0));
    CHECK (Lighting::intensityAt (s, 2) == Lighting::intensityAt (s, 1));
}
void expiredAndInvalid()
{
    Lighting l;
    l.start (1000, 1, 0);
    const double nan = std::numeric_limits<double>::quiet_NaN();
    CHECK (!l.echo (0, 1, 999, 1000, 0));
    CHECK (!l.echo (0, 1, 1001, 1000, 0));
    CHECK (!l.echo (0, 1, nan, 1000, 0));
    CHECK (!l.echo (0, 1, 1000, nan, 0));
    CHECK (!l.echo (0, std::numeric_limits<float>::quiet_NaN(), 1000, 1000, 0));
    CHECK (!l.echo (0, 0, 1000, 1000, 0) && !l.echo (0, -1, 1000, 1000, 0));
    CHECK (!l.echo (7, 1, 1000, 1000, 0));
    CHECK (!l.echo (0, 1, 1000, 1000 + Lighting::echoMs, 0) && !l.echo (0, 1, 1000, 2000, 0));
    CHECK (!l.echo (0, 1, 1000, 1000, nan) && !l.echo (0, 1, 1000, 1000, -1));
    CHECK (!l.echo (0, 1, 1000, 1000, std::numeric_limits<double>::infinity()));
    CHECK (!l.sample (0, 1500).echoActive);
    CHECK (near (l.sample (0, 5275).idleProgress, 0.5)); // Invalid input never restarts idle.
    dark (l.sample (7, 5200));
    dark (l.sample (0, nan));
    l.syncEnabled (0, 10, nan);
    l.update (nan);
    CHECK (l.sample (0, 5200).enabled);
}
void restoreAndReopen()
{
    Lighting l;
    l.start (0, 1, 0);
    CHECK (l.echo (0, 1, 100, 100, 0.5));
    l.syncEnabled (2, 1, 110);
    auto s = l.sample (1, 110);
    CHECK (s.enabled && s.enableProgress < 0 && s.welcomeProgress < 0 && !s.echoActive);
    dark (l.sample (0, 110));
    CHECK (l.welcomeProgress (110) < 0);
    CHECK (!l.echo (1, 1, 109, 110, 0.5));
    CHECK (l.echo (1, 1, 110, 110, 0.5));
    l.syncEnabled (2, 1, 120);
    CHECK (l.sample (1, 120).echoActive);
    l.syncEnabled (2, 2, 130);
    CHECK (!l.sample (1, 130).echoActive && !l.echo (1, 1, 120, 130, 0.5));
    l.stop();
    dark (l.sample (1, 2000));
    CHECK (l.welcomeProgress (2000) < 0 && !l.echo (1, 1, 2000, 2000, 0.5));
    l.start (2000, 0, 2);
    CHECK (l.sample (1, 2000).welcomeProgress == 0);
    CHECK (near (l.welcomeProgress (2750), 0.5));
    CHECK (Lighting::cellLevel (l.sample (1, 2750), 36) == Level::peak);
    dark (l.sample (1, 3500));
}
void clockOwnershipAndPureSamples()
{
    Lighting l;
    l.start (1000, 1, 0);
    l.update (1200);
    CHECK (l.echo (0, 1, 1100, 1200, 0.5));
    const Lighting& readOnly = l; // Query purity also checked by the compiler.
    const auto reference = readOnly.sample (0);
    for (double t : { 900.0, 1216.0, 100000.0, 1100.0, 999.0 })
    {
        for (std::size_t lane = 0; lane < 7; ++lane) (void) readOnly.sample (lane, t);
        (void) readOnly.welcomeProgress (t);
        (void) readOnly.intensityAt (0, 0.5f, t);
        const auto unchanged = readOnly.sample (0);
        CHECK (unchanged.echoCount == reference.echoCount);
        CHECK (unchanged.echoes[0].progress == reference.echoes[0].progress);
        CHECK (unchanged.welcomeProgress == reference.welcomeProgress);
    }
    (void) readOnly.sample (0, 1216); // Paint newer than timer snapshot MUST NOT reset on sync.
    l.syncEnabled (1, 0, 1200);
    CHECK (readOnly.sample (0).echoActive && readOnly.welcomeProgress() > 0);
    CHECK (!l.echo (0, 1, 1100, 1199, 0.5)); // Stale receipt does not silently rebase.
    CHECK (readOnly.sample (0).echoActive);
    l.advance (900); // Only an explicit update/reset owns backwards-clock policy.
    const auto reset = readOnly.sample (0);
    CHECK (reset.enabled && !reset.echoActive && reset.enableProgress < 0 && reset.welcomeProgress < 0);
    CHECK (reset.idleProgress < 0 && readOnly.welcomeProgress() < 0);
    CHECK (!l.echo (0, 1, 1100, 900, 0.5) && !l.echo (0, 1, 850, 900, 0.5));
    CHECK (near (readOnly.sample (0, 5175).idleProgress, 0.5));
    l.syncEnabled (2, 0, 100);
    CHECK (readOnly.sample (1).enabled && readOnly.sample (1).enableProgress < 0);
    dark (readOnly.sample (0));
}
std::array<Lighting::Sample, 6> cadence (double frameMs)
{
    Lighting l;
    l.start (0, 3, 0);
    double last = 0;
    const auto paintUntil = [&] (double now)
    {
        for (double t = last + frameMs; t < now; t += frameMs)
            for (std::size_t lane = 0; lane < 7; ++lane) (void) l.sample (lane, t);
        last = now;
        l.update (now);
    };
    std::array<Lighting::Sample, 6> result;
    paintUntil (750); result[0] = l.sample (0);
    paintUntil (2000); l.syncEnabled (7, 0, 2000);
    paintUntil (2350); result[1] = l.sample (2);
    paintUntil (4050); CHECK (l.echo (0, 0.6f, 4000, 4050, 0.5));
    paintUntil (4120); CHECK (l.echo (0, 0.9f, 4110, 4120, 8.25));
    paintUntil (4260); result[2] = l.sample (0);
    paintUntil (4530); result[3] = l.sample (0);
    paintUntil (8805); result[4] = l.sample (0);
    paintUntil (9000); l.syncEnabled (4, 1, 9000); result[5] = l.sample (2);
    return result;
}
void frameRateIndependent()
{
    const auto sparse = cadence (100000), fps30 = cadence (1000.0 / 30), fps144 = cadence (1000.0 / 144);
    for (const auto& run : { fps30, fps144 })
        for (std::size_t i = 0; i < sparse.size(); ++i)
        {
            const auto& a = sparse[i]; const auto& b = run[i];
            CHECK (a.enabled == b.enabled && a.echoActive == b.echoActive && a.echoCount == b.echoCount);
            CHECK (a.steady == b.steady && a.idle == b.idle && a.flash == b.flash);
            CHECK (a.welcomeProgress == b.welcomeProgress && a.enableProgress == b.enableProgress && a.idleProgress == b.idleProgress);
            for (int cell = 0; cell <= 64; ++cell)
            {
                const auto x = Lighting::cellLevels (a, cell, 4, 6), y = Lighting::cellLevels (b, cell, 4, 6);
                CHECK (x.edge == y.edge && x.center == y.center && x.target == y.target && x.echoActualCell == y.echoActualCell);
            }
        }
}
} // namespace

int main()
{
    timeGrid(); fiveLevelsAndSettings(); welcome(); offAndEnable(); idle(); realEchoAndIdleRestart();
    overlapAndBounds(); expiredAndInvalid(); restoreAndReopen(); clockOwnershipAndPureSamples(); frameRateIndependent();
    std::cout << "lighting_tests: 11 groups passed, " << checks << " checks\n";
}
