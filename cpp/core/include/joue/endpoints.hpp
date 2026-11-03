#pragma once
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "joue/transfer.hpp"

namespace joue {

struct EndpointInfo {
    std::uint32_t ref = 0;
    bool isSource = false;
    int index = 0;
    std::string name, displayName, entity, device, manufacturer, model;
    std::uint32_t entityRef = 0;

    std::string allText() const { return name + " | " + displayName + " | " + entity + " | " + device + " | " + manufacturer + " | " + model; }
};

struct PortPair {
    EndpointInfo source, destination;
    std::string text() const { return "in \"" + source.displayName + "\" / out \"" + destination.displayName + "\""; }
};

// Case and accent insensitive, so "Joué" and "joue" both match.
bool looksLikeJoue(const EndpointInfo& endpoint);

// Pairs inputs with outputs of the same entity; if the entity grouping does not work, pairs by order.
std::vector<PortPair> findPairs(const std::vector<EndpointInfo>& endpoints);

enum class PortRole { Editor, Play, Bootloader, Other };

// Role of a port by its name, with the names the official J-Play application matches: macOS "Joue Edit" / "Joue Play",
// Windows "MIDIIN2 (Joue)" / "MIDIOUT2 (Joue)" for the editor port and "Joue", "Joue-2" for the play port, Linux
// "Joue MIDI 2" / "Joue MIDI 1", and "Joue Bootloader" everywhere.
PortRole roleOf(const EndpointInfo& endpoint);

// The Windows editor opens the second port pair ("MIDIIN2" / "MIDIOUT2"); prefer a pair whose names say so,
// otherwise the second pair, otherwise the only one. Returns the index, or -1 when there is no pair.
int chooseEditorPair(const std::vector<PortPair>& pairs);

// Platform backend (CoreMIDI on macOS). Both throw std::runtime_error where unsupported.
std::vector<EndpointInfo> listEndpoints();
// inputOnly: the output of the pair is not opened (the Play port is only listened to; holding its output could keep other programs from it).
std::unique_ptr<MidiPort> openPort(const PortPair& pair, bool inputOnly = false);

}  // namespace joue
