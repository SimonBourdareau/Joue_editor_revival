#pragma once
#include <array>
#include <atomic>
#include <deque>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "joue/bytes.hpp"
#include "joue/endpoints.hpp"
#include "joue/mixer.hpp"
#include "joue/transfer.hpp"

class MixerBridge {
public:
    enum class Kind { FromScaler, ToDaw, FromDaw, ToExtender, FromExtender, Note };

    struct Line {
        Kind kind = Kind::FromScaler;
        std::string text;
    };

    struct View {
        joue::mixer::Snapshot snapshot;
        joue::mixer::SurfaceState host;
        joue::mixer::SurfaceState extender;
        joue::mixer::Options options;
        int shownStrips = joue::mixer::StripCount;
        std::vector<Line> lines;
        unsigned long long received = 0, sent = 0, sentExtender = 0, fromDaw = 0, fromExtender = 0;
        bool connected = false;
        double connectedForMs = 0.0;
        bool pairsSwapped = false;
        bool pairsLocked = false;
        int deviceOnFirstPair = 0, deviceOnSecondPair = 0;
        std::string connection, problem;
    };

    MixerBridge();
    ~MixerBridge();
    MixerBridge(const MixerBridge&) = delete;
    MixerBridge& operator=(const MixerBridge&) = delete;

    void fromSurface(const joue::Bytes& message);
    void fromHost(const joue::Bytes& message, int unit = 0);
    bool connectVirtual(const std::string& name, const std::string& secondName, std::string& error);
    bool connect(const joue::EndpointInfo& toDaw, const joue::EndpointInfo& fromDaw, const joue::EndpointInfo* toExtender, const joue::EndpointInfo* fromExtender, std::string& error);
    void attach(std::unique_ptr<joue::MidiPort> toDaw, std::unique_ptr<joue::MidiPort> fromDaw, std::unique_ptr<joue::MidiPort> toExtender, std::unique_ptr<joue::MidiPort> fromExtender, const std::string& text);
    void disconnect();
    bool connected() const { return connected_; }
    View view();
    void clearLog();
    void setBubbleSideways(joue::mixer::BubbleSideways mode);
    joue::mixer::BubbleSideways bubbleSideways();
    void press(int mackieNote, int unit = 0);
    void setBankStep(joue::mixer::BankStep step);
    void setPairsSwapped(bool swapped);
    void setFaderMode(joue::mixer::FaderMode mode);
    joue::mixer::BankStep bankStep();
    void setOptions(const joue::mixer::Options& options);
    void goToFirstTrack();

    std::atomic<bool> enabled{false};
    std::atomic<bool> paused{false};
    std::atomic<bool> showIncoming{true};
    std::function<void(const joue::Bytes&)> output;

private:
    void run();
    void deliver(const std::vector<joue::Bytes>& messages);
    void log(Kind kind, const joue::Bytes& message);
    void note(const std::string& text);
    static double nowMs();

    std::mutex mutex_;
    joue::mixer::Bridge bridge_;
    std::deque<Line> lines_;
    unsigned long long received_ = 0, sent_ = 0, sentExtender_ = 0, fromDaw_ = 0, fromExtender_ = 0;
    std::unique_ptr<joue::MidiPort> toDawPort_, fromDawPort_, toExtenderPort_, fromExtenderPort_;
    std::atomic<bool> connected_{false};
    double connectedAtMs_ = 0.0;
    std::atomic<bool> swapped_{false};
    std::atomic<bool> swapLocked_{false};
    std::array<int, 2> deviceSeen_{};
    std::string connection_, problem_;
    std::deque<int> queued_;
    double nextQueuedMs_ = 0.0;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};
