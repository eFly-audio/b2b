// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// Captures the DAW's own audio output (this process only). macOS 13+: ScreenCaptureKit.
// Windows 10 2004+: WASAPI process loopback. Elsewhere: not supported.
#include <functional>
#include <string>

namespace appcapture {
using Callback = std::function<void(const float* L, const float* R, int numFrames)>;
// "" on success, else a message for the user. sampleRate = the host's rate.
std::string start(double sampleRate, Callback cb);
void stop();
double actualRate(); // rate the system actually delivers (0 = nothing yet)
}
