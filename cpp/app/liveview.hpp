#pragma once
#include <array>
#include <atomic>
#include <deque>
#include <condition_variable>
#include <functional>
#include <optional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "joue/designer.hpp"
#include "joue/endpoints.hpp"
#include "joue/live.hpp"
#include "joue/modulewrite.hpp"
#include "texture.hpp"

// A window that shows the current state of the instrument: the device picture with the module of every slot drawn on it,
// found by reading the slots every two seconds, and the fingers on the surface as circles that grow with the pressure.
class LiveView {
public:
    bool open = false;
    std::atomic<bool> listenToPlay{true};  // listens to the Play port (the touches); off, the program does not hold that port at all
    bool safeGraphics = false;  // draws the device without a texture (for graphics drivers that show a texture black)
    ~LiveView();


    // Called with the slot and the bytes read when the user asks to edit the layout of a module.
    std::function<void(int, const joue::Bytes&)> onEdit;
    // Pushing an edited layout into the module of a slot; the work is done by the thread that owns the connection.
    bool canPush(int slot);
    // A layout sent to the board without writing it to a module (see Device::loadTemporarily): shown in slot 1 as a temporary module.
    bool canLoadTemporarily();
    // A layout written to a blank tag lying in a slot (see joue::writeBlankTag).
    bool canWriteTag();
    // True once the board said it is a Play board: its modules can be read but not changed, so nothing is written to them.
    bool playBoard();
    // True when the board announced a module in the slot whose tag turned out to be a new, blank one.
    bool newTagIn(int slot);
    void requestWriteTag(int slot, const joue::Bytes& layout);
    // Erases the tag in a slot, only when it holds a custom layout (see joue::blankTag).
    void requestEraseTag(int slot);
    void requestTemporaryLoad(const joue::Bytes& blob);   // the instrument is connected and announced a module in that slot
    bool pushBusy();
    // The module and element where the last finger came down, once: nullopt when there is none that was not reported yet.
    struct TouchedModule {
        int type = 0;
        int firstSlot = 0;
        std::size_t record = 0;
        unsigned sequence = 0;
        bool temporary = false;  // the module is the temporary layout sent from the designer
    };
    std::optional<TouchedModule> takeTouched();
    void requestDeviceInfo();
    std::vector<std::pair<std::string, std::string>> deviceInfo();
    bool deviceInfoBusy();
    // What the live view noticed, newest last: what the board announced, what was read for each slot and why a read was refused, and when the
    // connection was lost. Shown in the live panel so that a wrong detection can be told apart from a wrong display.
    std::vector<std::string> logLines();
    // Asks the board to announce its modules again and reads their layouts again (the same as at the first connection).
    void requestRescan();
    struct LiveModule {
        int slot = 0;
        joue::Bytes raw;  // the bytes read from the module
    };
    std::vector<LiveModule> modules();  // the modules detected now
    void requestPush(int slot, const joue::Bytes& original, const joue::Bytes& edited);
    std::string pushStatus();  // progress or result of the last push, empty when there was none

    // Draws the instrument as a panel of the given height inside the window being built (nothing when it is hidden).
    void drawPanel(float height);

private:
    friend struct LiveViewTestAccess;  // the tests drive the scan with a simulated board
    struct Shown {
        std::vector<joue::PlacedModule> placed;
        std::vector<joue::Design> designs;
        std::array<bool, 3> newTags{};  // a slot that holds a new, blank tag: not a module
        std::vector<std::pair<int, std::string>> unreadable;  // a module the board announced whose layout could not be read: slot, what the board said
    };

    void startWorker();
    void stopWorker();
    void workerMain();
    bool connect(std::string& error);
    void disconnect();
    void scanOnce(bool urgent);
    void apply(const std::array<joue::SlotInfo, joue::SlotCount>& slots);
    static double nowMs();

    // worker side
    std::thread worker_;
    std::atomic<bool> stop_{false}, scanNow_{false};
    std::mutex wakeMutex_;
    std::condition_variable wake_;
    std::unique_ptr<joue::MidiPort> editPort_, playPort_;
    std::unique_ptr<joue::TransferClient> client_;
    std::unique_ptr<joue::Device> device_;
    joue::SlotFilter filter_;  // worker thread only
    std::unique_ptr<joue::SlotScanner> scanner_;
    std::array<joue::Bytes, joue::SlotCount> blobs_;  // worker thread only: the blob read for each announced slot
    std::array<unsigned, joue::SlotCount> blobStamp_{};
    double connectedAtMs_ = 0;
    int portMisses_ = 0;
    joue::PortPair playPair_;
    bool havePlayPair_ = false;
    bool openPlay();
    std::string playSource_;  // the name of the Play port being listened to
    long editMessages_ = 0;   // channel messages that arrived on the Edit port
    joue::Bytes lastEditMessage_;
    bool everConnected_ = false;
    double lastPortCheckMs_ = 0;
    std::string editSource_, editDestination_;  // the names of the ports in use, to notice when they go away
    bool portsStillThere();

    // shared with the user interface
    std::mutex mutex_;
    std::mutex logMutex_;
    std::deque<std::string> log_;
    double logStartMs_ = 0;
    std::atomic<bool> rescanNow_{false};
    unsigned reportedTouch_ = 0;  // guarded by mutex_
    std::atomic<bool> infoRequested_{false};
    std::vector<std::pair<std::string, std::string>> deviceInfo_;  // guarded by mutex_
    bool infoBusy_ = false;  // guarded by mutex_
    double virtualRebaseUntilMs_ = 0;  // after a rescan the board announces its modules again; until then the temporary layout is not judged by them, guarded by mutex_
    std::atomic<int> limitations_{-1};  // the limitation flags of the board, -1 until it has answered
    std::array<std::string, 3> unreadable_{}, unreadReason_{}, shownUnreadable_{};  // worker thread only
    std::array<bool, 3> newTags_{}, shownNewTags_{};  // worker thread only
    std::array<int, 3> readTries_{};        // failed tries to read the layout announced in a slot, since its last insertion; worker thread only
    std::array<double, 3> nextTryMs_{};     // not before this time
    std::array<unsigned, 3> triedStamp_{};  // the insertion those counts are about
    std::array<unsigned, 3> blankStamp_{};  // the insertion of a slot that holds a new tag, which is not a module, has been looked at
    std::array<unsigned, 3> refusedStamp_{};  // the insertion of a slot for which no matching layout could be read has been logged
    void note(const std::string& text);
    std::shared_ptr<const Shown> shown_ = std::make_shared<const Shown>();  // replaced as a whole, so the window only copies a pointer
    joue::TouchTracker tracker_;
    std::string status_;
    double lastScanMs_ = 0;
    long messages_ = 0;  // messages received on the Play port
    joue::Bytes lastMessage_;
    long kindCounts_[7] = {};         // messages seen by kind: note off, note on, poly pressure, controller, program, channel pressure, pitch bend
    long controllerCounts_[128] = {};  // controllers seen by number
    bool connected_ = false, isPlay_ = false, touchesAvailable_ = false, fallback_ = false;
    joue::Announcements announced_;  // what the board announced about its slots

    struct PushJob {
        int slot = 0;
        joue::Bytes original, edited;
    };
    struct TagJob {
        int slot = 0;
        joue::Bytes layout;
        bool erase = false;
    };
    std::optional<TagJob> tagJob_;  // guarded by mutex_
    std::optional<PushJob> pushJob_;  // guarded by mutex_
    std::optional<joue::Bytes> temporaryJob_;  // guarded by mutex_
    std::array<unsigned, joue::SlotCount> virtualChanges_{};  // the announcements of the slots when the temporary layout was sent; guarded by mutex_
    joue::Bytes virtualBlob_;  // the temporary layout shown in slot 1, empty when there is none; guarded by mutex_
    bool pushing_ = false;
    std::string pushStatus_;

    // user interface side
    // A device picture: the pixels of the embedded PNG and the texture made from them at the size nearest above the one it is shown at.
    struct Picture {
        std::vector<unsigned char> rgba;
        int width = 0, height = 0;
        Texture texture;
        bool decoded = false, failed = false;
    };
    Picture pro_, play_;
    const Texture& pictureFor(Picture& picture, const unsigned char* png, std::size_t size, float wantedPixels);
};
