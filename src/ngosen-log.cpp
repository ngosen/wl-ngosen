/*
 * SPDX-FileCopyrightText: 2026 Ngó Sen contributors
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 */
#include "ngosen-log.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace ngosen {

    namespace {
        // Lets the tests read what the daemon logs.
        FILE* logFile() {
            static FILE* file = [] {
                const char* path = std::getenv("NGOSEN_LOG_FILE");
                FILE*       out  = path != nullptr ? std::fopen(path, "ae") : nullptr;
                return out != nullptr ? out : stderr;
            }();
            return file;
        }

        const char* levelName(LogLevel level) {
            switch (level) {
                case LogLevel::Debug: return "D";
                case LogLevel::Info: return "I";
                case LogLevel::Warn: return "W";
                case LogLevel::Error: return "E";
            }
            return "?";
        }
    } // namespace

    // NGOSEN_DEBUG=1 in the daemon's environment turns on the debug and info lines.
    bool logEnabled(LogLevel level) {
        static const bool verbose = [] {
            const char* value = std::getenv("NGOSEN_DEBUG");
            return value != nullptr && *value != '\0' && std::strcmp(value, "0") != 0;
        }();
        return verbose || level == LogLevel::Warn || level == LogLevel::Error;
    }

    // The compositor that starts the daemon keeps its stderr in its own log or the journal.
    void writeLog(LogLevel level, const char* file, int line, const std::string& message) {
        const char* slash = std::strrchr(file, '/');
        std::fprintf(logFile(), "wl-ngosen %s %s:%d %s\n", levelName(level), slash != nullptr ? slash + 1 : file, line, message.c_str());
        std::fflush(logFile());
    }

} // namespace ngosen
