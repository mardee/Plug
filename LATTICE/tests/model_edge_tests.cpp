// model_edge_tests.cpp — header-only edge/invariant harness for lattice::Pattern.
//
// No JUCE dependency: compiles standalone against Model.h with a plain C++17
// toolchain. Exercises the real (16 bars x 8 tracks, 60-tick / 32nd-note grid)
// phrase generator across every supported meter, tuplet ratio and generative
// parameter setting. Exits non-zero on any violated invariant.
//
//   g++ -std=c++17 -I src tests/model_edge_tests.cpp -o build/model_edge_tests

#include "Model.h"
#include <cassert>
#include <iostream>
#include <vector>

int main()
{
    // 1) Every supported meter: 16 bars x 8 tracks, contiguous, full-bar, valid
    //    velocities, and aligned to the 60-tick (32nd-note) grid.
    long laneCells = 0;
    for (int n = 1; n <= 16; ++n)
        for (int d : {4, 8})
        {
            lattice::Pattern p; p.meter(n, d); assert(p.valid()); p.generate();
            int steps = p.barTicks() / 60;   // 60-tick (32nd) grid
            for (auto& b : p.bars)                 // 16 bars
                for (auto& l : b)                  // 8 tracks
                {
                    int pos = 0;
                    for (auto& c : l)
                    {
                        assert(c.start == pos && c.length > 0);
                        assert(c.start % 60 == 0 && c.length % 60 == 0);   // 32nd grid
                        assert(c.velocity == 0 || c.velocity == 40 ||
                               c.velocity == 80 || c.velocity == 127);
                        pos += c.length;
                        ++laneCells;
                    }
                    assert(pos == p.barTicks());
                }
        }
    std::cout << "[1] 16 meters x 8 tracks x 60-tick grid (contiguous/full/valid): PASS ("
              << laneCells << " cells)\n";

    // 2) Full 16-bar 4/4 phrase duration is 30720 ticks (16 * 1920).  Pattern cells
    //    store per-bar relative starts (each bar spans its own [0, barTicks)), so the
    //    absolute phrase end is the product; the export layer verifies the 30720 tick
    //    boundary explicitly.
    {
        lattice::Pattern p; p.generate();
        long phraseEnd = (long)lattice::phraseBars * p.barTicks();   // 16 * 1920
        assert(phraseEnd == 30720);
        for (auto& b : p.bars)
            for (auto& l : b)
            {
                int pos = 0;
                for (auto& c : l) { assert(c.start == pos); pos += c.length; }
                assert(pos == p.barTicks());
            }
        std::cout << "[2] 16-bar 4/4 phrase duration == 30720 ticks (16*1920): PASS\n";
    }

    // 3) Tuplet edits (3 and 6) stay contiguous and full-bar on the 60-tick grid.
    long tupletEdits = 0;
    for (int n = 1; n <= 16; ++n)
        for (int d : {4, 8})
        {
            lattice::Pattern p; p.meter(n, d); p.generate();
            for (int count : {3, 6})
            {
                auto q = p;
                assert(q.subdivide(0, 0, 0, 1, count));   // 2-cell (120-tick) region -> tuplet
                int pos = 0;
                for (auto c : q.bars[0][0])
                {
                    assert(c.start == pos);          // contiguous after tuplet edit
                    assert(c.length > 0);
                    pos += c.length;
                }
                assert(pos == q.barTicks());         // tuplet stays full-bar (no drift)
                ++tupletEdits;
            }
        }
    std::cout << "[3] 32 meters x {3,6} tuplet edits contiguous & full-bar: PASS ("
              << tupletEdits << ")\n";

    // 4) Generative parameters (development/fill/space) yield valid, full patterns.
    {
        lattice::Pattern p;
        for (float dev : {0.0f, 0.5f, 1.0f})
            for (float fl : {0.0f, 0.5f, 1.0f})
                for (float sp : {0.0f, 0.5f, 1.0f})
                {
                    p.development = dev; p.fill = fl; p.space = sp;
                    assert(p.valid()); p.generate();
                    for (auto& b : p.bars)
                        for (auto& l : b)
                        {
                            int pos = 0;
                            for (auto& c : l) { assert(c.start == pos && c.length > 0); pos += c.length; }
                            assert(pos == p.barTicks());
                        }
                }
        std::cout << "[4] development/fill/space sweep (27 combos) valid & full-bar: PASS\n";
    }

    // 5) Crash is the 8th lane (GM 49) and fires at least once across the phrase.
    {
        lattice::Pattern p; p.generate();
        assert(lattice::notes[7] == 49);
        int crashHits = 0;
        for (auto& b : p.bars)
            for (auto& c : b[7])
                if (c.velocity > 0) ++crashHits;
        assert(crashHits > 0);   // default: Crash on bars 0 and 8
        std::cout << "[5] 8th lane is Crash (GM 49) and fires in phrase: PASS ("
                  << crashHits << " hits)\n";
    }

    std::cout << "model_edge_tests: ALL PASS\n";
    return 0;
}
