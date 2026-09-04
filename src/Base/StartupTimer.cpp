// SPDX-License-Identifier: LGPL-2.1-or-later
/****************************************************************************
 *                                                                          *
 *   Copyright (c) 2026 FreeCAD Project Association                         *
 *                                                                          *
 *   This file is part of FreeCAD.                                          *
 *                                                                          *
 *   FreeCAD is free software: you can redistribute it and/or modify it     *
 *   under the terms of the GNU Lesser General Public License as            *
 *   published by the Free Software Foundation, either version 2.1 of the   *
 *   License, or (at your option) any later version.                        *
 *                                                                          *
 *   FreeCAD is distributed in the hope that it will be useful, but         *
 *   WITHOUT ANY WARRANTY; without even the implied warranty of             *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU       *
 *   Lesser General Public License for more details.                        *
 *                                                                          *
 *   You should have received a copy of the GNU Lesser General Public       *
 *   License along with FreeCAD. If not, see                                *
 *   <https://www.gnu.org/licenses/>.                                       *
 *                                                                          *
 ***************************************************************************/

#include "StartupTimer.h"

#include <cstdio>
#include <string>
#include <vector>

#include "Console.h"

using namespace Base;

namespace
{
// Lines produced before the console had any observer; see StartupTimer::flushDeferred
std::vector<std::string>& deferredLines()
{
    static std::vector<std::string> lines;
    return lines;
}

bool loggingIsUp = false;

// Fixed the first time any phase is timed, which is as close to process start as the timers
// get. Everything is reported relative to it so the phases can be placed on one timeline.
const std::chrono::steady_clock::time_point processStart = std::chrono::steady_clock::now();

long long millisecondsBetween(std::chrono::steady_clock::time_point from,
                              std::chrono::steady_clock::time_point to)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(to - from).count();
}
}  // namespace

StartupTimer::StartupTimer(const char* phase)
    : phase(phase)
    , started(std::chrono::steady_clock::now())
{}

StartupTimer::~StartupTimer()
{
    const auto now = std::chrono::steady_clock::now();
    // "at" is when the phase ended on the shared timeline; a gap between one phase's "at" and
    // the next one's "at" minus its duration is time nobody is measuring.
    char line[160];
    std::snprintf(line,
                  sizeof(line),
                  "Startup: %-32s %6lld ms   (at %6lld ms)\n",
                  phase,
                  millisecondsBetween(started, now),
                  millisecondsBetween(processStart, now));
    if (loggingIsUp) {
        Base::Console().log("%s", line);
    }
    else {
        deferredLines().emplace_back(line);
    }
}

void StartupTimer::flushDeferred()
{
    loggingIsUp = true;
    for (const auto& line : deferredLines()) {
        Base::Console().log("%s", line.c_str());
    }
    deferredLines().clear();
}

long long StartupTimer::sinceStart()
{
    return millisecondsBetween(processStart, std::chrono::steady_clock::now());
}
