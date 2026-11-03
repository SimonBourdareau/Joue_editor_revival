// Linux backend on ALSA raw MIDI: every port of every sound card with MIDI is listed, and a port is opened as "hw:card,device,sub".
#ifdef JOUE_HAVE_ALSA
#include <alsa/asoundlib.h>
#include <poll.h>

#include <atomic>
#include <stdexcept>
#include <thread>

#include "joue/endpoints.hpp"
#include "joue/midiparse.hpp"

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

    ~AlsaMidiPort() override {
        stop_ = true;
        reader_.join();
        snd_rawmidi_close(in_);
        snd_rawmidi_close(out_);
    }

    void send(const Bytes& sysex) override {
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

}  // namespace

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

}  // namespace joue
#endif  // JOUE_HAVE_ALSA
