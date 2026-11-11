// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// macOS-only helpers (implemented in MacSupport.mm). Plain C++ interface so the
// rest of the code base stays portable.
#include <cstdint>
#include <string>
#include <vector>

namespace macsupport {

// True if the host app's Info.plist declares the given usage key
// (e.g. "NSCameraUsageDescription"). Touching the camera/mic without it makes
// macOS terminate the whole DAW, so always check first.
bool hostDeclares(const char* infoPlistKey);

// Screen recording permission (granted to the DAW, not the plugin).
bool hasScreenAccess();
void requestScreenAccess(); // shows the system prompt once

// Displays as "Display 1 (3024x1964)" etc.
std::vector<std::string> screenNames();

// Grab one frame of display `index`, scaled to fit maxW x maxH, cursor included.
// Output is BGRA, tightly packed, even width/height. Returns false on failure.
bool grabScreen(int index, int maxW, int maxH, std::vector<uint8_t>& bgra, int& w, int& h);

} // namespace macsupport
