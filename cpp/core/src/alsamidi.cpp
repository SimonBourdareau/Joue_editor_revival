// Linux backend on ALSA raw MIDI: every port of every sound card with MIDI is listed, and a port is opened as "hw:card,device,sub".
#ifdef JOUE_HAVE_ALSA
#include <alsa/asoundlib.h>
#include <poll.h>

#include <atomic>
#include <stdexcept>
#include <thread>

#include "joue/endpoints.hpp"
#include "joue/midiparse.hpp"
#include "virtualcore.hpp"

namespace joue {

namespace {

// ref packs card, device and subdevice so that a port can be reopened later.
std::uint32_t pack(int card, int device, int sub) { return (static_cast<std::uint32_t>(card) << 16) | (static_cast<std::uint32_t>(device) << 8) | static_cast<std::uint32_t>(sub); }

std::string hwName(std::uint32_t ref) {
    return "hw:" + std::to_string(ref >> 16) + "," + std::to_string((ref >> 8) & 0xFF) + "," + std::to_string(ref & 0xFF);
}

class AlsaMidiPort : public MidiPort {
public:
    explicit AlsaMidiPort(const PortPair& pair) {
        int status = snd_rawmidi_open(&in_, nullptr, hwName(pair.source.ref).c_str(), SND_RAWMIDI_NONBLOCK);
        if (status < 0) throw std::runtime_error(std::string("snd_rawmidi_open (input): ") + snd_strerror(status));
        status = snd_rawmidi_open(nullptr, &out_, hwName(pair.destination.ref).c_str(), 0);
        if (status < 0) {
            snd_rawmidi_close(in_);
            throw std::runtime_error(std::string("snd_rawmidi_open (output): ") + snd_strerror(status));
        }
        reader_ = std::thread([this] { run(); });
    }

    AlsaMidiPort(const EndpointInfo& endpoint, bool outputSide) {
        if (outputSide) {
            int status = snd_rawmidi_open(nullptr, &out_, hwName(endpoint.ref).c_str(), 0);
            if (status < 0) throw std::runtime_error(std::string("snd_rawmidi_open (output): ") + snd_strerror(status));
            return;
        }
        int status = snd_rawmidi_open(&in_, nullptr, hwName(endpoint.ref).c_str(), SND_RAWMIDI_NONBLOCK);
        if (status < 0) throw std::runtime_error(std::string("snd_rawmidi_open (input): ") + snd_strerror(status));
        reader_ = std::thread([this] { run(); });
    }

    ~AlsaMidiPort() override {
        stop_ = true;
        if (reader_.joinable()) reader_.join();
        if (in_) snd_rawmidi_close(in_);
        if (out_) snd_rawmidi_close(out_);
    }

    void send(const Bytes& sysex) override {
        if (!out_) throw std::runtime_error("This port was opened for listening only.");
        std::size_t done = 0;
        while (done < sysex.size()) {
            ssize_t n = snd_rawmidi_write(out_, sysex.data() + done, sysex.size() - done);
            if (n < 0) throw std::runtime_error(std::string("snd_rawmidi_write: ") + snd_strerror(static_cast<int>(n)));
            done += static_cast<std::size_t>(n);
        }
        snd_rawmidi_drain(out_);
    }

private:
    void run() {
        int count = snd_rawmidi_poll_descriptors_count(in_);
        std::vector<pollfd> fds(static_cast<std::size_t>(count));
        snd_rawmidi_poll_descriptors(in_, fds.data(), static_cast<unsigned>(count));
        unsigned char buffer[1024];
        while (!stop_) {
            int ready = poll(fds.data(), static_cast<nfds_t>(count), 50);
            if (ready <= 0) continue;
            unsigned short revents = 0;
            snd_rawmidi_poll_descriptors_revents(in_, fds.data(), static_cast<unsigned>(count), &revents);
            if (!(revents & POLLIN)) continue;
            ssize_t n = snd_rawmidi_read(in_, buffer, sizeof buffer);
            if (n > 0) {
                for (const Bytes& message : assembler_.feed(buffer, static_cast<std::size_t>(n))) deliverAny(message);
            }
        }
    }

    snd_rawmidi_t* in_ = nullptr;
    snd_rawmidi_t* out_ = nullptr;
    MessageParser assembler_;
    std::thread reader_;
    std::atomic<bool> stop_{false};
};

// A port of the ALSA sequencer that other programs connect to (aconnect, or the DAW's own port list).
class AlsaVirtualCore : public VirtualCore {
public:
    AlsaVirtualCore(const std::string& name, std::string& error) {
        int status = snd_seq_open(&seq_, "default", SND_SEQ_OPEN_DUPLEX, 0);
        if (status < 0) {
            error = std::string("snd_seq_open: ") + snd_strerror(status);
            seq_ = nullptr;
            return;
        }
        snd_seq_set_client_name(seq_, name.c_str());
        port_ = snd_seq_create_simple_port(seq_, name.c_str(), SND_SEQ_PORT_CAP_READ | SND_SEQ_PORT_CAP_SUBS_READ | SND_SEQ_PORT_CAP_WRITE | SND_SEQ_PORT_CAP_SUBS_WRITE,
                                           SND_SEQ_PORT_TYPE_MIDI_GENERIC | SND_SEQ_PORT_TYPE_APPLICATION);
        if (port_ < 0) {
            error = std::string("snd_seq_create_simple_port: ") + snd_strerror(port_);
            snd_seq_close(seq_);
            seq_ = nullptr;
            return;
        }
        snd_midi_event_new(4096, &encoder_);
        snd_midi_event_new(4096, &decoder_);
        snd_midi_event_no_status(decoder_, 1);
        reader_ = std::thread([this] { run(); });
    }

    ~AlsaVirtualCore() override {
        stop_ = true;
        if (reader_.joinable()) reader_.join();
        if (encoder_) snd_midi_event_free(encoder_);
        if (decoder_) snd_midi_event_free(decoder_);
        if (seq_) snd_seq_close(seq_);
    }

    bool ok() const { return seq_ != nullptr; }

    void write(const Bytes& message) override {
        if (message.empty()) return;
        snd_seq_event_t event;
        snd_seq_ev_clear(&event);
        if (message[0] == 0xF0) {
            snd_seq_ev_set_sysex(&event, static_cast<unsigned>(message.size()), const_cast<unsigned char*>(message.data()));
        } else {
            snd_midi_event_reset_encode(encoder_);
            long used = snd_midi_event_encode(encoder_, message.data(), static_cast<long>(message.size()), &event);
            if (used <= 0 || event.type == SND_SEQ_EVENT_NONE) return;
        }
        snd_seq_ev_set_source(&event, static_cast<unsigned char>(port_));
        snd_seq_ev_set_subs(&event);
        snd_seq_ev_set_direct(&event);
        int status = snd_seq_event_output_direct(seq_, &event);
        if (status < 0) throw std::runtime_error(std::string("snd_seq_event_output_direct: ") + snd_strerror(status));
    }

private:
    void run() {
        int count = snd_seq_poll_descriptors_count(seq_, POLLIN);
        std::vector<pollfd> fds(static_cast<std::size_t>(count));
        snd_seq_poll_descriptors(seq_, fds.data(), static_cast<unsigned>(count), POLLIN);
        while (!stop_) {
            if (poll(fds.data(), static_cast<nfds_t>(count), 50) <= 0) continue;
            do {
                snd_seq_event_t* event = nullptr;
                if (snd_seq_event_input(seq_, &event) < 0 || !event) break;
                if (event->type == SND_SEQ_EVENT_SYSEX) {
                    const unsigned char* data = static_cast<const unsigned char*>(event->data.ext.ptr);
                    received(Bytes(data, data + event->data.ext.len));
                } else {
                    unsigned char buffer[16];
                    snd_midi_event_reset_decode(decoder_);
                    long used = snd_midi_event_decode(decoder_, buffer, sizeof buffer, event);
                    if (used > 0) received(Bytes(buffer, buffer + used));
                }
            } while (snd_seq_event_input_pending(seq_, 0) > 0);
        }
    }

    snd_seq_t* seq_ = nullptr;
    int port_ = -1;
    snd_midi_event_t* encoder_ = nullptr;
    snd_midi_event_t* decoder_ = nullptr;
    std::thread reader_;
    std::atomic<bool> stop_{false};
};

}  // namespace

std::unique_ptr<VirtualCore> makeVirtualCore(const std::string& name, std::string& error) {
    auto core = std::make_unique<AlsaVirtualCore>(name, error);
    if (!core->ok()) return nullptr;
    return core;
}

bool virtualCoreAvailable(std::string&) { return true; }

std::vector<EndpointInfo> listEndpoints() {
    std::vector<EndpointInfo> result;
    int sourceIndex = 0, destinationIndex = 0;
    int card = -1;
    while (snd_card_next(&card) >= 0 && card >= 0) {
        std::string cardName = "hw:" + std::to_string(card);
        snd_ctl_t* control = nullptr;
        if (snd_ctl_open(&control, cardName.c_str(), 0) < 0) continue;
        char* longName = nullptr;
        std::string cardLabel;
        if (snd_card_get_name(card, &longName) == 0 && longName) {
            cardLabel = longName;
            free(longName);
        }
        int device = -1;
        while (snd_ctl_rawmidi_next_device(control, &device) >= 0 && device >= 0) {
            snd_rawmidi_info_t* info;
            snd_rawmidi_info_alloca(&info);
            snd_rawmidi_info_set_device(info, static_cast<unsigned>(device));
            for (int pass = 0; pass < 2; ++pass) {
                bool isSource = pass == 0;
                snd_rawmidi_info_set_stream(info, isSource ? SND_RAWMIDI_STREAM_INPUT : SND_RAWMIDI_STREAM_OUTPUT);
                snd_rawmidi_info_set_subdevice(info, 0);
                if (snd_ctl_rawmidi_info(control, info) < 0) continue;
                unsigned subdevices = snd_rawmidi_info_get_subdevices_count(info);
                for (unsigned sub = 0; sub < subdevices; ++sub) {
                    snd_rawmidi_info_set_subdevice(info, sub);
                    if (snd_ctl_rawmidi_info(control, info) < 0) continue;
                    EndpointInfo endpoint;
                    endpoint.ref = pack(card, device, static_cast<int>(sub));
                    endpoint.isSource = isSource;
                    endpoint.index = isSource ? sourceIndex++ : destinationIndex++;
                    endpoint.name = snd_rawmidi_info_get_subdevice_name(info);
                    endpoint.displayName = cardLabel + ": " + endpoint.name;
                    endpoint.entity = snd_rawmidi_info_get_name(info);
                    endpoint.device = cardLabel;
                    endpoint.entityRef = (static_cast<std::uint32_t>(card) << 16) | (static_cast<std::uint32_t>(device) << 8) | sub | 0x80000000u;
                    result.push_back(endpoint);
                }
            }
        }
        snd_ctl_close(control);
    }
    return result;
}

std::unique_ptr<MidiPort> openPort(const PortPair& pair, bool) { return std::make_unique<AlsaMidiPort>(pair); }

std::unique_ptr<MidiPort> openOutputPort(const EndpointInfo& destination) { return std::make_unique<AlsaMidiPort>(destination, true); }

std::unique_ptr<MidiPort> openInputPort(const EndpointInfo& source) { return std::make_unique<AlsaMidiPort>(source, false); }

}  // namespace joue
#endif  // JOUE_HAVE_ALSA
