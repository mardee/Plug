#pragma once

#include "../core/MidiTypes.h"

#include <algorithm>
#include <cmath>

namespace glint
{
// One shared horizontal coordinate system for all seven lanes. There are 64
// intervals but 65 drawable positions, including both ends of the four-beat page.
struct NightShiftTimeGrid
{
    static constexpr int stepsPerBeat = 16, stepsPerWindow = 64, positionCount = 65;
    static constexpr double stepBeats = 0.0625, windowBeats = 4.0;

    static constexpr bool validCell (int cell) noexcept { return cell >= 0 && cell < positionCount; }
    static constexpr float normalizedX (int cell) noexcept
    {
        return static_cast<float> (std::clamp (cell, 0, stepsPerWindow)) / stepsPerWindow;
    }
    static constexpr double beatForCell (int cell) noexcept
    {
        return validCell (cell) ? cell * stepBeats : -1.0;
    }
    static int choiceCell (int choice) noexcept
    {
        if (choice < 0 || choice >= static_cast<int> (ozo::midi::noteBeats.size())) return -1;
        return static_cast<int> (std::lround (ozo::midi::noteBeats[static_cast<std::size_t> (choice)] * stepsPerBeat));
    }

    // Parameter marks are settings, not predictions or evidence of emitted MIDI.
    // Page zero is the default; its right endpoint may be a target at beat four.
    static bool isTargetCell (int firstChoice, int repeatChoice, int cell, double page = 0.0) noexcept
    {
        const int first = choiceCell (firstChoice), repeat = choiceCell (repeatChoice);
        if (!validCell (cell) || first < 0 || repeat <= 0 || !std::isfinite (page)
            || page < 0.0 || page != std::floor (page)) return false;
        const double offset = page * stepsPerWindow + cell - first;
        return std::isfinite (offset) && offset >= 0.0 && std::fmod (offset, repeat) == 0.0;
    }
    static bool isTarget (int cell, int firstChoice, int repeatChoice, double page = 0.0) noexcept
    {
        return isTargetCell (firstChoice, repeatChoice, cell, page);
    }

    // Actual timing is relative to each input-triggered cloud, never a shared
    // transport clock. Show its mod-four position; expose page separately so the
    // UI can label the wrap. Exactly four beats belongs to page one, position zero.
    static double pageForOnset (double onsetBeats) noexcept
    {
        return std::isfinite (onsetBeats) && onsetBeats >= 0.0 ? std::floor (onsetBeats / windowBeats) : -1.0;
    }
    static int cellForOnset (double onsetBeats) noexcept
    {
        if (pageForOnset (onsetBeats) < 0.0) return -1;
        return std::clamp (static_cast<int> (std::lround (std::fmod (onsetBeats, windowBeats) * stepsPerBeat)),
                           0, stepsPerWindow);
    }
    static int cellForBeat (double beat) noexcept { return cellForOnset (beat); }
};
} // namespace glint
