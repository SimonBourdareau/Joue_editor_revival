// Platforms without a native MIDI backend: listing and opening report that MIDI is unavailable.
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(JOUE_HAVE_ALSA)
#include <stdexcept>

#include "joue/endpoints.hpp"
#include "virtualcore.hpp"

namespace joue {

std::vector<EndpointInfo> listEndpoints() { throw std::runtime_error("No MIDI backend is available on this platform."); }

std::unique_ptr<MidiPort> openPort(const PortPair&, bool) { throw std::runtime_error("No MIDI backend is available on this platform."); }

std::unique_ptr<MidiPort> openOutputPort(const EndpointInfo&) { throw std::runtime_error("No MIDI backend is available on this platform."); }

std::unique_ptr<MidiPort> openInputPort(const EndpointInfo&) { throw std::runtime_error("No MIDI backend is available on this platform."); }

std::unique_ptr<VirtualCore> makeVirtualCore(const std::string&, std::string& error) {
    error = "This system cannot create MIDI ports.";
    return nullptr;
}

bool virtualCoreAvailable(std::string& explanation) {
    explanation = "This system cannot create MIDI ports.";
    return false;
}

}  // namespace joue
#endif
