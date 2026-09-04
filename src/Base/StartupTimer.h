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

#pragma once

#include <chrono>

#include <FCGlobal.h>

namespace Base
{

/** Logs how long a phase of startup took.
 *
 * Startup is felt as one wait by the user, and these are what tell which of its phases the
 * wait is made of. The line goes to the log rather than to the user: on a healthy start there
 * is nothing to say, and the numbers exist to be read when something is slow.
 *
 * Unlike the Tracy zones in Profiler.h this costs a clock read and a log line, so it stays in
 * release builds, which are the ones users report slow starts from.
 */
class BaseExport StartupTimer
{
public:
    explicit StartupTimer(const char* phase);
    ~StartupTimer();

    StartupTimer(const StartupTimer&) = delete;
    StartupTimer& operator=(const StartupTimer&) = delete;
    StartupTimer(StartupTimer&&) = delete;
    StartupTimer& operator=(StartupTimer&&) = delete;

    /// Milliseconds since the process began timing its startup
    static long long sinceStart();

    /** Reports the phases timed before there was anywhere to report them to.
     * The log has no observers until the configuration has been read, so anything timed
     * before that point is held back and written out by this, once logging works.
     */
    static void flushDeferred();

private:
    const char* phase;
    std::chrono::steady_clock::time_point started;
};

}  // namespace Base
