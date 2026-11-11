// b2b by eFly Audio - real-time collaboration plugin
// Copyright (C) 2026 eFly Audio
// SPDX-License-Identifier: GPL-3.0-only  (see LICENSE)

#pragma once
// Shared between b2b and b2b MIDI (separate plugins, same machine): b2b MIDI drops
// finished takes into the outbox, b2b picks them up and sends them to the collaborator.
#include <juce_core/juce_core.h>

namespace b2bpaths {
inline juce::File root() { return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile("b2b"); }
inline juce::File midiOutbox() { return root().getChildFile("MIDI Outbox"); }
inline juce::File midiTakes() { return root().getChildFile("MIDI Takes"); }
} // namespace b2bpaths
