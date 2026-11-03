// Platforms without a native MIDI backend: listing and opening report that MIDI is unavailable.
#if !defined(__APPLE__) && !defined(_WIN32) && !defined(JOUE_HAVE_ALSA)
#include <stdexcept>

#include "joue/endpoints.hpp"

namespace joue {

std::vector<EndpointInfo> listEndpoints() { throw std::runtime_error("No MIDI backend is available on this platform."); }

std::unique_ptr<MidiPort> openPort(const PortPair&, bool) { throw std::runtime_error("No MIDI backend is available on this platform."); }

}  // namespace joue
#endif
