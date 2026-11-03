#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#endif
#include "liveview.hpp"
#include "fonts.hpp"

#include <algorithm>
#include <stdexcept>
#include <thread>
#include <chrono>
#include <cmath>

#define IMGUI_DEFINE_MATH_OPERATORS
#include "blobdraw.hpp"
#include "imgui.h"
#include "joue/blob.hpp"
#include "joue/deviceinfo.hpp"
#include "joue/endpoints.hpp"
#include "joue/i18n.hpp"
#include "joue/moduleart.hpp"

using joue::i18n::tr;

namespace {
#include "device_pictures.inc"

void dim(const std::string& text) {
    ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextWrapped("%s", text.c_str());
    ImGui::PopStyleColor();
}

std::string describeMessage(const joue::Bytes& m) {
    if (m.size() < 2) return "";
    int channel = (m[0] & 0x0F) + 1;
    std::string where = "channel " + std::to_string(channel);
    switch (m[0] & 0xF0) {
        case 0x80: return "note off, " + where + ", note " + std::to_string(m[1]);
        case 0x90: return std::string(m.size() > 2 && m[2] ? "note on" : "note off") + ", " + where + ", note " + std::to_string(m[1]) + (m.size() > 2 ? ", velocity " + std::to_string(m[2]) : "");
        case 0xA0: return "poly pressure, " + where + ", note " + std::to_string(m[1]) + (m.size() > 2 ? ", value " + std::to_string(m[2]) : "");
        case 0xB0: return "controller " + std::to_string(m[1]) + ", " + where + (m.size() > 2 ? ", value " + std::to_string(m[2]) : "");
        case 0xC0: return "program change, " + where;
        case 0xD0: return "channel pressure, " + where + ", value " + std::to_string(m[1]);
        case 0xE0: return "pitch bend, " + where + (m.size() > 2 ? ", value " + std::to_string(((m[2] << 7) | m[1]) - 8192) : "");
        default: return "";
    }
}

// The touch surface inside each picture, as fractions of its width and height.
struct SurfaceBox {
    float x, y, w, h;
};
const SurfaceBox ProSurface = {9.5f / 202.0f, 13.5f / 78.0f, 150.0f / 202.0f, 50.0f / 78.0f};
const SurfaceBox PlaySurface = {10.0f / 202.0f, 10.5f / 73.0f, 150.0f / 202.0f, 50.0f / 73.0f};
// The device drawn with rectangles, for when no texture can be used: wood bands, the dark body, the touch surface and its dividers.
void drawDeviceByHand(ImDrawList* list, ImVec2 origin, ImVec2 size, bool play, const SurfaceBox& box) {
    float band = size.y * (play ? 6.0f / 73.0f : 9.0f / 78.0f);
    ImU32 woodLight = play ? IM_COL32(236, 200, 146, 255) : IM_COL32(196, 160, 138, 255);
    ImU32 woodDark = play ? IM_COL32(205, 160, 100, 255) : IM_COL32(120, 76, 46, 255);
    list->AddRectFilled(origin, origin + size, IM_COL32(27, 27, 28, 255));
    list->AddRectFilledMultiColor(origin, origin + ImVec2(size.x, band), woodLight, woodLight, woodDark, woodDark);
    list->AddRectFilledMultiColor(origin + ImVec2(0, size.y - band), origin + size, woodDark, woodDark, woodLight, woodLight);
    ImVec2 s0 = origin + ImVec2(size.x * box.x, size.y * box.y), s1 = s0 + ImVec2(size.x * box.w, size.y * box.h);
    list->AddRectFilled(s0, s1, IM_COL32(38, 38, 42, 255));
    for (int k = 1; k <= 2; ++k) {
        float x = s0.x + (s1.x - s0.x) * k / 3.0f;
        list->AddRectFilled(ImVec2(x - size.x * 0.0028f, s0.y + size.y * 0.04f), ImVec2(x + size.x * 0.0028f, s1.y - size.y * 0.04f), IM_COL32(150, 150, 158, 255), 2.0f);
    }
}
}  // namespace

double LiveView::nowMs() {
    using namespace std::chrono;
    return duration<double, std::milli>(steady_clock::now().time_since_epoch()).count();
}

LiveView::~LiveView() {
    stopWorker();
    destroyTexture(pro_.texture);
    destroyTexture(play_.texture);
}

const Texture& LiveView::pictureFor(Picture& picture, const unsigned char* png, std::size_t size, float wantedPixels) {
    static const Texture none;
    if (picture.failed) return none;
    if (!picture.decoded) {
        picture.decoded = true;
        if (!decodePng(png, size, picture.rgba, picture.width, picture.height)) picture.failed = true;
    }
    if (picture.failed) return none;
    // the stored picture is ten times the size of the original; the version nearest above the size it is shown at is used, so that it is
    // only reduced by a factor between 1 and 2 and plain linear filtering stays clean
    int level = 0;
    while (level < 3 && (picture.width >> (level + 1)) >= wantedPixels) ++level;
    int width = picture.width >> level, height = picture.height >> level;
    if (picture.texture.id == 0 || picture.texture.width != width) {
        destroyTexture(picture.texture);
        picture.texture = level == 0 ? createTextureRgba(picture.rgba.data(), picture.width, picture.height)
                                     : createTextureRgba(resizeRgba(picture.rgba, picture.width, picture.height, width, height).data(), width, height);
        if (picture.texture.id == 0) picture.failed = true;
    }
    return picture.texture;
}

bool LiveView::canPush(int slot) {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && !pushing_ && !pushJob_ && announced_.present(slot);
}

std::vector<LiveView::LiveModule> LiveView::modules() {
    std::shared_ptr<const Shown> shown;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shown = shown_;
    }
    std::vector<LiveModule> out;
    for (const joue::PlacedModule& m : shown->placed) out.push_back({m.slot, m.raw});
    return out;
}

bool LiveView::canLoadTemporarily() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && !pushing_ && !pushJob_ && !temporaryJob_ && !tagJob_;
}

bool LiveView::canWriteTag() {
    std::lock_guard<std::mutex> lock(mutex_);
    return connected_ && !pushing_ && !pushJob_ && !temporaryJob_ && !tagJob_;
}

void LiveView::requestEraseTag(int slot) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pushing_ || pushJob_ || temporaryJob_ || tagJob_) return;
        TagJob job;
        job.slot = slot;
        job.erase = true;
        tagJob_ = job;
        pushStatus_ = "Waiting to erase the tag in slot " + std::to_string(slot) + "...";
    }
    wake_.notify_all();
}

void LiveView::requestWriteTag(int slot, const joue::Bytes& layout) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pushing_ || pushJob_ || temporaryJob_ || tagJob_) return;
        tagJob_ = TagJob{slot, layout};
        pushStatus_ = "Waiting to write the tag in slot " + std::to_string(slot) + "...";
    }
    wake_.notify_all();
}

void LiveView::requestTemporaryLoad(const joue::Bytes& blob) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pushing_ || pushJob_ || temporaryJob_ || tagJob_) return;
        temporaryJob_ = blob;
        pushStatus_ = "Waiting to send the temporary layout...";
    }
    wake_.notify_all();
}

void LiveView::requestDeviceInfo() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        infoBusy_ = true;
    }
    infoRequested_ = true;
    wake_.notify_all();
}

std::vector<std::pair<std::string, std::string>> LiveView::deviceInfo() {
    std::lock_guard<std::mutex> lock(mutex_);
    return deviceInfo_;
}

bool LiveView::deviceInfoBusy() {
    std::lock_guard<std::mutex> lock(mutex_);
    return infoBusy_;
}

std::optional<LiveView::TouchedModule> LiveView::takeTouched() {
    std::lock_guard<std::mutex> lock(mutex_);
    joue::TouchedElement touched = tracker_.lastTouched();
    if (touched.sequence == 0 || touched.sequence == reportedTouch_) return std::nullopt;
    reportedTouch_ = touched.sequence;
    if (!shown_ || touched.module >= shown_->placed.size()) return std::nullopt;
    const joue::PlacedModule& m = shown_->placed[touched.module];
    TouchedModule out;
    out.type = m.blob.header.type;
    out.firstSlot = m.firstSlot;
    out.record = touched.record;
    out.sequence = touched.sequence;
    out.temporary = !virtualBlob_.empty() && m.raw == virtualBlob_;
    return out;
}

bool LiveView::pushBusy() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pushing_ || pushJob_.has_value() || temporaryJob_.has_value() || tagJob_.has_value();
}

void LiveView::requestPush(int slot, const joue::Bytes& original, const joue::Bytes& edited) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pushing_ || pushJob_) return;
        pushJob_ = PushJob{slot, original, edited};
        pushStatus_ = "Waiting to write to slot " + std::to_string(slot) + "...";
    }
    wake_.notify_all();
}

std::string LiveView::pushStatus() {
    std::lock_guard<std::mutex> lock(mutex_);
    return pushStatus_;
}

void LiveView::note(const std::string& text) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (logStartMs_ == 0) logStartMs_ = nowMs();
    int seconds = static_cast<int>((nowMs() - logStartMs_) / 1000.0);
    char stamp[24];
    std::snprintf(stamp, sizeof stamp, "%02d:%02d  ", seconds / 60, seconds % 60);
    log_.push_back(std::string(stamp) + text);
    while (log_.size() > 40) log_.pop_front();
}

std::vector<std::string> LiveView::logLines() {
    std::lock_guard<std::mutex> lock(logMutex_);
    return std::vector<std::string>(log_.begin(), log_.end());
}

void LiveView::requestRescan() {
    rescanNow_ = true;
    wake_.notify_all();
}

void LiveView::startWorker() {
    if (worker_.joinable()) return;
    stop_ = false;
    worker_ = std::thread([this] { workerMain(); });
}

void LiveView::stopWorker() {
    if (!worker_.joinable()) return;
    {
        std::lock_guard<std::mutex> lock(wakeMutex_);
        stop_ = true;
    }
    wake_.notify_all();
    worker_.join();
    disconnect();
}

// Without the instrument there is no layout: the modules, the touches and what the board announced are all dropped.
void LiveView::disconnect() {
    scanner_.reset();
    device_.reset();
    client_.reset();
    playPort_.reset();
    editPort_.reset();
    havePlayPair_ = false;
    blobs_ = {};
    blobStamp_ = {};
    filter_ = joue::SlotFilter();
    portMisses_ = 0;
    apply(std::array<joue::SlotInfo, joue::SlotCount>{});
    std::lock_guard<std::mutex> lock(mutex_);
    connected_ = false;
    touchesAvailable_ = false;
    announced_.clear();
    fallback_ = false;
    tracker_.clear();
    messages_ = 0;
    lastMessage_.clear();
    virtualBlob_.clear();
}

// True while the two ports of the instrument are still among those the system lists.
bool LiveView::portsStillThere() {
    try {
        bool otherJoue = false;
        for (const joue::PortPair& p : joue::findPairs(joue::listEndpoints())) {
            if (p.source.name == editSource_ && p.destination.name == editDestination_) return true;
            otherJoue = otherJoue || joue::roleOf(p.source) == joue::PortRole::Editor || joue::roleOf(p.source) == joue::PortRole::Play;
        }
        // the ports of the connection are not listed under their names but a port of a Joue is: the system renamed them, which is not an unplugged instrument
        if (otherJoue) return true;
    } catch (const std::exception&) {
        return true;  // the list could not be made: do not take that for an unplugged instrument
    }
    return false;
}

// Opens the Play port for listening only; false when it cannot be opened (another program may hold it).
bool LiveView::openPlay() {
    if (!havePlayPair_) return false;
    try {
        playSource_ = playPair_.source.name;
        playPort_ = joue::openPort(playPair_, true);
        playPort_->setMessageHandler([this](const joue::Bytes& m) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++messages_;
            lastMessage_ = m;
            if ((m[0] & 0xF0) >= 0x80 && (m[0] & 0xF0) <= 0xE0) ++kindCounts_[((m[0] & 0xF0) >> 4) - 8];
            if ((m[0] & 0xF0) == 0xB0 && m.size() > 1) ++controllerCounts_[m[1] & 0x7F];
            tracker_.feed(m, nowMs());
        });
        return true;
    } catch (const std::exception&) {
        playPort_.reset();
        return false;
    }
}

bool LiveView::connect(std::string& error) {
    try {
        auto pairs = joue::findPairs(joue::listEndpoints());
        std::vector<joue::PortPair> mine;
        for (const joue::PortPair& p : pairs) {
            if (joue::looksLikeJoue(p.source) || joue::looksLikeJoue(p.destination)) mine.push_back(p);
        }
        int edit = joue::chooseEditorPair(mine);
        if (edit < 0) {
            error = "No Joue found on the MIDI ports.";
            return false;
        }
        editSource_ = mine[static_cast<std::size_t>(edit)].source.name;
        editDestination_ = mine[static_cast<std::size_t>(edit)].destination.name;
        editPort_ = joue::openPort(mine[static_cast<std::size_t>(edit)]);
        editPort_->setMessageHandler([this](const joue::Bytes& m) {
            std::lock_guard<std::mutex> lock(mutex_);
            ++editMessages_;
            lastEditMessage_ = m;
        });
        client_ = std::make_unique<joue::TransferClient>(*editPort_, std::chrono::milliseconds(700));
        client_->setInfoHandler([this](const joue::Bytes& name) {
            if (auto event = joue::parseInfoMessage(name)) {
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    announced_.apply(*event);
                }
                note("the board announced slot " + std::to_string(event->slot) + ": " + (event->inserted ? joue::moduleName(event->type) + " (type " + std::to_string(event->type) + ", version " + std::to_string(event->version) + ", colour " + std::to_string(event->color) + ")" : std::string("module removed")));
            }
            scanNow_ = true;
        });
        device_ = std::make_unique<joue::Device>(*client_);
        scanner_ = std::make_unique<joue::SlotScanner>(*client_, *device_);
        int play = -1;
        for (std::size_t i = 0; i < mine.size(); ++i) {
            if (static_cast<int>(i) != edit && joue::roleOf(mine[i].source) == joue::PortRole::Play) play = static_cast<int>(i);
        }
        if (play < 0) {
            for (std::size_t i = 0; i < mine.size(); ++i) {
                if (static_cast<int>(i) != edit) play = static_cast<int>(i);
            }
        }
        havePlayPair_ = play >= 0;
        if (havePlayPair_) playPair_ = mine[static_cast<std::size_t>(play)];
        bool touches = listenToPlay && openPlay();
        auto hardware = device_->hardwareId();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            announced_.clear();
            fallback_ = false;
        }
        blobs_ = {};
        blobStamp_ = {};
        filter_ = joue::SlotFilter();
        scanner_->forget();
        connectedAtMs_ = nowMs();
        // like the official application: ask the board to announce every module again, then trust what it announces
        device_->updateModuleList();
        std::lock_guard<std::mutex> lock(mutex_);
        isPlay_ = hardware && ((*hardware >> 8) & 1) != 0;
        touchesAvailable_ = touches;
        tracker_.clear();
        messages_ = 0;
        editMessages_ = 0;
        lastEditMessage_.clear();
        lastMessage_.clear();
        std::fill(std::begin(kindCounts_), std::end(kindCounts_), 0L);
        std::fill(std::begin(controllerCounts_), std::end(controllerCounts_), 0L);
        connected_ = true;
        return true;
    } catch (const std::exception& e) {
        error = e.what();
        disconnect();
        return false;
    }
}

void LiveView::apply(const std::array<joue::SlotInfo, joue::SlotCount>& slots) {
    auto shown = std::make_shared<Shown>();
    shown->placed = joue::placeModules(slots);
    for (const joue::PlacedModule& m : shown->placed) shown->designs.push_back(joue::Design::fromBytes(m.blob.toBytes()));
    std::lock_guard<std::mutex> lock(mutex_);
    shown_ = shown;
    tracker_.setModules(shown->placed);
}

void LiveView::scanOnce(bool urgent) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        lastScanMs_ = nowMs();
    }
    joue::Announcements announced;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        announced = announced_;
    }
    joue::Bytes virtualBlob;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!virtualBlob_.empty() && nowMs() < virtualRebaseUntilMs_) {
            // a rescan: the board announces the modules that were already there again, which is not a module put on or taken off
            for (int slot = 1; slot <= joue::SlotCount; ++slot) virtualChanges_[static_cast<std::size_t>(slot - 1)] = announced_.changes(slot);
        } else if (!virtualBlob_.empty() && joue::temporaryLayoutOutlived(virtualChanges_, announced_)) {
            // a module, or a custom tag, was put on the instrument or taken off it: the layout that was loaded without a tag is gone
            virtualBlob_.clear();
            note("a module was put on the instrument or taken off: the temporary layout is gone");
            pushStatus_ = "The temporary layout is gone: a module was put on or taken off.";
        }
        virtualBlob = virtualBlob_;
    }
    std::array<joue::SlotInfo, joue::SlotCount> slots;
    if (announced.any()) {
        // the board says which slots hold a module; a blob is read only for those, and again only for a new insertion
        for (int slot = 1; slot <= joue::SlotCount; ++slot) {
            std::size_t i = static_cast<std::size_t>(slot - 1);
            if (!announced.present(slot)) {
                blobs_[i].clear();
                continue;
            }
            if (!joue::moduleMayAnswerFrom(joue::moduleSlots(announced.event(slot).type), slot)) {
                // a module of three slots cannot answer from slot 2 or 3, nor one of two from slot 3: not a module that is there
                blobs_[i].clear();
                if (refusedStamp_[i] != announced.stamp(slot)) {
                    refusedStamp_[i] = announced.stamp(slot);
                    note("slot " + std::to_string(slot) + ": the board announced a " + joue::moduleName(announced.event(slot).type) + ", which takes " + std::to_string(joue::moduleSlots(announced.event(slot).type)) + " slots and cannot answer from slot " + std::to_string(slot) + "; it is not shown");
                }
                continue;
            }
            if (blobs_[i].empty() || blobStamp_[i] != announced.stamp(slot)) {
                blobs_[i].clear();
                client_->setTimeout(std::chrono::milliseconds(2500));
                joue::ModuleEvent event = announced.event(slot);
                // a read of a slot can hand back the module of another one: what is kept for a slot must be the type of
                // module the board announced there, and a read that is not is made again
                for (int attempt = 0; attempt < 3 && blobs_[i].empty(); ++attempt) {
                    auto blob = device_->readModule(slot);
                    if (blob && blob->size() >= joue::InstrumentHeader::Length) {
                        try {
                            joue::ModuleBlob::parse(*blob);
                            if (joue::blobFitsAnnouncement(*blob, event)) {
                                blobs_[i] = *blob;
                                blobStamp_[i] = announced.stamp(slot);
                                note("slot " + std::to_string(slot) + ": layout read, " + joue::moduleName(event.type) + ", " + std::to_string(blob->size()) + " bytes");
                            } else {
                                note("slot " + std::to_string(slot) + ": a read gave a " + joue::moduleName((*blob)[0]) + " but the board announced a " + joue::moduleName(event.type) + "; it was not kept");
                            }
                        } catch (const std::exception&) {
                        }
                    }
                    if (blobs_[i].empty() && attempt < 2) std::this_thread::sleep_for(std::chrono::milliseconds(300));
                }
                if (blobs_[i].empty() && refusedStamp_[i] != announced.stamp(slot)) {
                    refusedStamp_[i] = announced.stamp(slot);
                    note("slot " + std::to_string(slot) + ": no layout that fits the announcement could be read; the module is not shown (try Rescan modules)");
                }
            }
            if (!blobs_[i].empty()) slots[i] = {true, blobs_[i]};
        }
        {
            std::lock_guard<std::mutex> lock(mutex_);
            fallback_ = false;
        }
        if (!virtualBlob.empty() && !slots[0].present) slots[0] = {true, virtualBlob};
        if (filter_.offer(slots, true)) apply(filter_.accepted());
        return;
    }
    if (nowMs() - connectedAtMs_ < 4000.0) return;  // the board has not had time to announce its modules yet
    // the board announced nothing: read the slots directly, cheaply, as a fallback
    {
        std::lock_guard<std::mutex> lock(mutex_);
        fallback_ = true;
    }
    slots = scanner_->scan(filter_);
    if (!virtualBlob.empty() && (slots[0].present || slots[1].present || slots[2].present)) {
        // without announcements a module found by the scan is the sign that one was put on the instrument
        {
            std::lock_guard<std::mutex> lock(mutex_);
            virtualBlob_.clear();
        }
        virtualBlob.clear();
        note("a module was found on the instrument: the temporary layout is gone");
    }
    if (!virtualBlob.empty() && !slots[0].present) slots[0] = {true, virtualBlob};
    if (filter_.offer(slots, urgent)) apply(filter_.accepted());
}

void LiveView::workerMain() {
    double lastAttempt = -1e9;
    while (!stop_) {
        bool connected;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            connected = connected_;
        }
        if (!connected && nowMs() - lastAttempt > 3000.0) {
            lastAttempt = nowMs();
            std::string error;
            bool ok = connect(error);
            std::lock_guard<std::mutex> lock(mutex_);
            status_ = ok ? "Connected." : (everConnected_ ? std::string("Joué Disconnected") : error);
            {
                // the line is logged when it changes, not at every attempt
                static std::string lastConnectNote;
                std::string text = ok ? std::string("connected to the Joue") : std::string("not connected: ") + error;
                if (text != lastConnectNote) note(text);
                lastConnectNote = text;
            }
            if (ok) {
                scanNow_ = true;
                everConnected_ = true;
            }
        }
        {
            // the switch "Listen to the Play port": the port is opened or closed here, on the thread that owns the connection
            bool want = listenToPlay, have;
            bool online;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                have = playPort_ != nullptr;
                online = connected_;
            }
            if (online && havePlayPair_ && want != have) {
                if (want) {
                    bool ok = openPlay();
                    std::lock_guard<std::mutex> lock(mutex_);
                    touchesAvailable_ = ok;
                } else {
                    playPort_.reset();
                    std::lock_guard<std::mutex> lock(mutex_);
                    touchesAvailable_ = false;
                    tracker_.clear();
                }
            }
        }
        std::optional<joue::Bytes> temporary;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (temporaryJob_ && connected_) {
                temporary = temporaryJob_;
                temporaryJob_.reset();
                pushing_ = true;
                pushStatus_ = "Sending the temporary layout...";
            }
        }
        if (temporary) {
            bool sent = false;
            std::string reason;
            try {
                client_->setTimeout(std::chrono::milliseconds(2500));
                sent = device_->loadTemporarily(*temporary);
            } catch (const std::exception& e) {
                reason = e.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pushing_ = false;
                if (sent) {
                    virtualBlob_ = *temporary;
                    for (int slot = 1; slot <= joue::SlotCount; ++slot) virtualChanges_[static_cast<std::size_t>(slot - 1)] = announced_.changes(slot);
                    pushStatus_ = "Sent " + std::to_string(temporary->size()) + " bytes as a temporary layout. It is shown in slot 1; play on the sensor. It is lost at a reset or when a module is inserted or removed.";
                } else {
                    pushStatus_ = "The board did not accept the temporary layout" + (reason.empty() ? std::string(".") : ": " + reason);
                }
            }
            scanNow_ = true;
        }
        if (rescanNow_.exchange(false)) {
            bool online;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                online = connected_;
            }
            if (online) {
                // the same as at the first connection: forget what was announced, ask the board to announce its modules again, give it time
                note("rescan: asking the board to announce its modules again");
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    announced_.clear();
                    connectedAtMs_ = nowMs();
                    virtualRebaseUntilMs_ = nowMs() + 6000.0;
                }
                blobs_ = {};
                refusedStamp_ = {};
                try {
                    client_->setTimeout(std::chrono::milliseconds(2500));
                    device_->updateModuleList();
                } catch (const std::exception&) {
                }
            }
        }
        if (infoRequested_.exchange(false)) {
            bool can;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                can = connected_ && !pushing_;
                if (!can) infoBusy_ = false;
            }
            if (can) {
                std::vector<std::pair<std::string, std::string>> lines;
                try {
                    lines = joue::describeDeviceInfo(joue::readDeviceInfo(*device_));
                } catch (const std::exception& e) {
                    lines.push_back({"Error", e.what()});
                }
                std::lock_guard<std::mutex> lock(mutex_);
                std::vector<std::pair<std::string, std::string>> all;
                all.push_back({"Editor port", editSource_});
                all.push_back({"Play port", playSource_.empty() ? std::string("not open") : playSource_});
                all.insert(all.end(), lines.begin(), lines.end());
                for (int slot = 1; slot <= joue::SlotCount; ++slot) {
                    std::string value = "empty";
                    if (announced_.present(slot)) {
                        joue::ModuleEvent e = announced_.event(slot);
                        value = joue::moduleName(e.type) + " (type " + std::to_string(e.type) + ", version " + std::to_string(e.version) + ", colour " + std::to_string(e.color) + ")";
                    }
                    all.push_back({"Module in slot " + std::to_string(slot), value});
                }
                deviceInfo_ = all;
                infoBusy_ = false;
            }
        }
        std::optional<TagJob> tag;
        bool slotHoldsModule = false;
        int announcedType = -1;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (tagJob_ && connected_) {
                tag = tagJob_;
                tagJob_.reset();
                pushing_ = true;
                pushStatus_ = std::string(tag->erase ? "Erasing" : "Writing") + " the tag in slot " + std::to_string(tag->slot) + "...";
                slotHoldsModule = announced_.present(tag->slot) && announced_.event(tag->slot).type != 0;
                announcedType = announced_.present(tag->slot) ? announced_.event(tag->slot).type : -1;
            }
        }
        if (tag) {
            joue::PushReport report;
            try {
                auto pause = [this](int ms) {
                    std::unique_lock<std::mutex> lock(wakeMutex_);
                    wake_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
                };
                if (tag->erase) report = joue::blankTag(*device_, *client_, tag->slot, announcedType, pause);
                else report = joue::writeBlankTag(*device_, *client_, tag->slot, tag->layout, slotHoldsModule, pause);
            } catch (const std::exception& e) {
                report.message = std::string("The write failed: ") + e.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pushing_ = false;
                pushStatus_ = report.message;
            }
            blobs_[static_cast<std::size_t>(tag->slot - 1)].clear();
            scanNow_ = true;
        }
        std::optional<PushJob> job;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (pushJob_ && connected_) {
                job = pushJob_;
                pushJob_.reset();
                pushing_ = true;
                pushStatus_ = "Writing to slot " + std::to_string(job->slot) + "...";
            }
        }
        if (job) {
            joue::PushReport report;
            try {
                report = joue::pushLayout(*device_, *client_, job->slot, job->original, job->edited, [this](int ms) {
                    std::unique_lock<std::mutex> lock(wakeMutex_);
                    wake_.wait_for(lock, std::chrono::milliseconds(ms), [this] { return stop_.load(); });
                });
            } catch (const std::exception& e) {
                report.message = std::string("The write failed: ") + e.what();
            }
            {
                std::lock_guard<std::mutex> lock(mutex_);
                pushing_ = false;
                pushStatus_ = report.message;
            }
            // the board reloads the module and announces it again; the blob is read again after that
            blobs_[static_cast<std::size_t>(job->slot - 1)].clear();
            scanNow_ = true;
        }
        bool ready;
        double lastScan;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ready = connected_;
            lastScan = lastScanMs_;
        }
        if (ready && nowMs() - lastPortCheckMs_ > 1000.0) {
            lastPortCheckMs_ = nowMs();
            // the ports must be missing from two lists in a row: one odd list is not an unplugged instrument
            portMisses_ = portsStillThere() ? 0 : portMisses_ + 1;
            if (portMisses_ >= 2) {
                portMisses_ = 0;
                note("the ports of the Joue are no longer listed: disconnected");
                disconnect();
                std::lock_guard<std::mutex> lock(mutex_);
                status_ = "Joué Disconnected";
                ready = false;
            }
        }
        if (ready) {
            double due = lastScan + 2000.0;
            if (scanNow_ || nowMs() >= due) {
                bool urgent = scanNow_;
                scanNow_ = false;
                try {
                    scanOnce(urgent);
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_ = "Connected; the slots are scanned every two seconds.";
                } catch (const std::exception& e) {
                    disconnect();
                    std::lock_guard<std::mutex> lock(mutex_);
                    status_ = "Joué Disconnected";
                }
            }
        }
        std::unique_lock<std::mutex> lock(wakeMutex_);
        wake_.wait_for(lock, std::chrono::milliseconds(150), [this] { return stop_.load(); });
    }
}

void LiveView::drawPanel(float height) {
    const bool mirror = joue::MirrorPictures;
    if (!open) {
        if (worker_.joinable()) stopWorker();
        return;
    }
    if (!worker_.joinable()) startWorker();
    float scale = ImGui::GetFontSize() / 15.0f;
    ImGui::BeginChild("instrument", ImVec2(0, height), ImGuiChildFlags_Borders);
    double now = nowMs();
    std::shared_ptr<const Shown> shown;
    std::vector<joue::Touch> touches;
    std::string status;
    bool play, touchesAvailable, connected;
    long messages;
    int unmatched, lastUnmatched;
    joue::Bytes lastMessage, lastEditMessage;
    long editMessages = 0;
    std::string editName, playName;
    joue::Bytes virtualBlob;
    joue::Announcements announced;
    bool fallback;
    std::vector<int> shifts;
    long kinds[7];
    long controllers[128];
    {
        std::lock_guard<std::mutex> lock(mutex_);
        shown = shown_;
        touches = tracker_.touches(now);
        status = status_;
        play = isPlay_;
        touchesAvailable = touchesAvailable_;
        connected = connected_;
        messages = messages_;
        lastMessage = lastMessage_;
        lastEditMessage = lastEditMessage_;
        editMessages = editMessages_;
        editName = editSource_;
        playName = playSource_;
        virtualBlob = virtualBlob_;
        announced = announced_;
        fallback = fallback_;
        shifts = tracker_.octaveShifts();
        std::copy(std::begin(kindCounts_), std::end(kindCounts_), std::begin(kinds));
        std::copy(std::begin(controllerCounts_), std::end(controllerCounts_), std::begin(controllers));
        unmatched = tracker_.unmatchedNoteOns();
        lastUnmatched = tracker_.lastUnmatchedNote();
    }
    ImGui::TextUnformatted(tr("Live instrument"));
    ImGui::SameLine();
    ImGui::TextDisabled("%s", joue::i18n::message(status.empty() ? std::string("Waiting for the instrument...") : status).c_str());
    ImGui::SameLine(ImGui::GetContentRegionMax().x - ImGui::CalcTextSize("Hide").x - 20.0f * scale);
    if (ImGui::SmallButton(tr("Hide"))) open = false;

    const SurfaceBox& box = play ? PlaySurface : ProSurface;
    ImVec2 avail = ImGui::GetContentRegionAvail();
    float aspect = (play ? 73.0f : 78.0f) / 202.0f;
    // the picture takes the height of the panel, at most 62% of its width; the lines about the instrument go to its right
    float width = std::min(avail.x * 0.62f, avail.y / aspect);
    width = std::max(width, 200.0f);
    ImVec2 size(width, width * aspect);
    ImVec2 origin = ImGui::GetCursorScreenPos();
    ImDrawList* list = ImGui::GetWindowDrawList();
    const Texture* texture = nullptr;
    if (!safeGraphics) {
        const Texture& t = play ? pictureFor(play_, kDevicePlayPng, sizeof kDevicePlayPng, width * ImGui::GetIO().DisplayFramebufferScale.x)
                                : pictureFor(pro_, kDeviceProPng, sizeof kDeviceProPng, width * ImGui::GetIO().DisplayFramebufferScale.x);
        if (t.id != 0) texture = &t;
    }
    if (texture) list->AddImage(static_cast<ImTextureID>(texture->id), origin, origin + size);
    else drawDeviceByHand(list, origin, size, play, box);
    if (!connected) {
        // no instrument, no layout: the picture is dimmed and says so
        list->AddRectFilled(origin, origin + size, IM_COL32(0, 0, 0, 150));
        const char* text = tr("Joué Disconnected");
        // drawn with the heading font, which has glyphs made for its size; the usual font enlarged would look blurred
        if (Fonts::heading) ImGui::PushFont(Fonts::heading);
        float fontSize = Fonts::heading ? ImGui::GetFontSize() : ImGui::GetFontSize() * 2.2f;
        ImVec2 extent = ImGui::GetFont()->CalcTextSizeA(fontSize, FLT_MAX, 0.0f, text);
        list->AddText(ImGui::GetFont(), fontSize, ImVec2(origin.x + (size.x - extent.x) * 0.5f, origin.y + (size.y - extent.y) * 0.5f), IM_COL32(235, 235, 235, 255), text);
        if (Fonts::heading) ImGui::PopFont();
    }
    ImVec2 surface = origin + ImVec2(size.x * box.x, size.y * box.y);
    float ppm = size.x * box.w / (3.0f * joue::SlotWidthMm);
    float surfaceHeight = joue::SurfaceHeightMm * ppm;

    for (std::size_t i = 0; i < shown->placed.size(); ++i) {
        const joue::PlacedModule& m = shown->placed[i];
        bool isVirtual = !virtualBlob.empty() && m.slot == 1 && m.raw == virtualBlob;
        int shift = i < shifts.size() ? shifts[i] : 0;
        ImVec2 corner = surface + ImVec2(static_cast<float>(m.firstSlot - 1) * joue::SlotWidthMm * ppm, 0);
        ImVec2 end = corner + ImVec2(static_cast<float>(m.slots) * joue::SlotWidthMm * ppm, surfaceHeight);
        list->AddRectFilled(corner + ImVec2(1, 1), end - ImVec2(1, 1), IM_COL32(20, 20, 24, 150), 3.0f);
        drawDesign(list, shown->designs[i], corner, ppm, mirror, 0.93f, shift);
        list->AddRect(corner + ImVec2(1, 1), end - ImVec2(1, 1), IM_COL32(255, 255, 255, 60), 3.0f);
        std::string name = joue::moduleName(m.blob.header.type) + (isVirtual ? std::string(" (") + tr("temporary") + ")" : std::string());
        if (shift != 0) name += "   octave " + std::string(shift > 0 ? "+" : "") + std::to_string(shift / 12);
        ImVec2 text = ImGui::CalcTextSize(name.c_str());
        list->AddRectFilled(corner + ImVec2(3, 3), corner + ImVec2(9 + text.x, 7 + text.y), IM_COL32(0, 0, 0, 160), 3.0f);
        list->AddText(corner + ImVec2(6, 5), IM_COL32(255, 255, 255, 230), name.c_str());
        {
            ImGui::SetCursorScreenPos(ImVec2(end.x - ImGui::CalcTextSize(tr("Edit")).x - 22.0f * scale, corner.y + 4.0f * scale));
            ImGui::PushID(1000 + static_cast<int>(i));
            if (!isVirtual && ImGui::SmallButton(tr("Edit")) && onEdit) onEdit(m.slot, m.raw);
            ImGui::PopID();
        }
        bool playsNotes = joue::hasOctaveButtons(m.blob);
        if (playsNotes) {
            // the board does not say when an octave button is pressed, so the shift is inferred; these correct it
            ImGui::SetCursorScreenPos(end - ImVec2(ImGui::CalcTextSize(tr("octave")).x + 70.0f * scale, 26.0f * scale));
            ImGui::PushID(static_cast<int>(i));
            if (ImGui::SmallButton("-")) {
                std::lock_guard<std::mutex> lock(mutex_);
                tracker_.setOctaveShift(i, shift - 12);
            }
            ImGui::SameLine();
            ImGui::TextUnformatted(tr("octave"));
            ImGui::SameLine();
            if (ImGui::SmallButton("+")) {
                std::lock_guard<std::mutex> lock(mutex_);
                tracker_.setOctaveShift(i, shift + 12);
            }
            ImGui::PopID();
        }
    }
    for (const joue::Touch& touch : touches) {
        float y = mirror ? joue::SurfaceHeightMm - touch.y : touch.y;
        ImVec2 c = surface + ImVec2(touch.x * ppm, y * ppm);
        float radius = ppm * (3.0f + 9.0f * touch.pressure);
        list->AddCircleFilled(c, radius, IM_COL32(255, 255, 255, 80), 40);
        list->AddCircle(c, radius, IM_COL32(255, 255, 255, 230), 40, 2.0f);
        list->AddCircleFilled(c, std::max(2.0f, ppm * 1.2f), IM_COL32(255, 80, 80, 255));
        std::string text = (touch.label.empty() ? std::string() : touch.label + "  ") + std::to_string(static_cast<int>(touch.pressure * 100.0f + 0.5f)) + "%";
        list->AddText(c + ImVec2(radius + 3, -8), IM_COL32(255, 255, 255, 230), text.c_str());
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy(size);
    ImGui::SameLine();
    ImGui::BeginChild("liveinfo", ImVec2(0, size.y), ImGuiChildFlags_None);

    if (connected && shown->placed.empty()) ImGui::TextDisabled("%s", tr("No module."));
    for (const joue::PlacedModule& m : shown->placed) {
        std::string slots = m.slots == 1 ? std::string(tr("Slot")) + " " + std::to_string(m.firstSlot) : std::string(tr("Slots")) + " " + std::to_string(m.firstSlot) + "-" + std::to_string(m.firstSlot + m.slots - 1);
        ImGui::Text(tr("%s: %s"), slots.c_str(), joue::moduleName(m.blob.header.type).c_str());
    }
    if (connected) {
        if (ImGui::SmallButton(tr("Rescan modules"))) requestRescan();
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tr("Ask the board for its modules again."));
    }
    // everything else is for finding out why something is wrong, and stays folded
    if (ImGui::TreeNode(tr("Details"))) {
        if (connected) {
            std::string line;
            if (announced.any()) {
                line = tr("Board:");
                for (int slot = 1; slot <= joue::SlotCount; ++slot) {
                    joue::ModuleEvent e = announced.event(slot);
                    line += " " + std::to_string(slot) + " " + (announced.present(slot) ? joue::moduleName(e.type) + " (" + tr("colour") + " " + std::to_string(e.color) + ")" : std::string(tr("empty"))) + (slot < joue::SlotCount ? "," : "");
                }
            } else if (fallback) {
                line = tr("The board announced nothing; slots read directly.");
            } else {
                line = tr("Asking the board for its modules...");
            }
            dim(line);
            std::string ports = "Ports: " + std::string(playName.empty() ? "no Play port" : "Play " + std::to_string(messages) + " messages") + ", Edit " + std::to_string(editMessages) + " messages";
            dim(joue::i18n::message(ports));
            if (touchesAvailable) {
                if (!lastMessage.empty()) dim(joue::i18n::message("Last: " + describeMessage(lastMessage)));
                if (unmatched > 0) dim(joue::i18n::message(std::to_string(unmatched) + " notes matched no element (last: note " + std::to_string(lastUnmatched) + ")"));
                static const char* kindNames[7] = {"note off", "note on", "poly pressure", "controllers", "program change", "channel pressure", "pitch bend"};
                std::string seen;
                for (int k = 0; k < 7; ++k) {
                    if (kinds[k] == 0) continue;
                    if (!seen.empty()) seen += ", ";
                    seen += std::string(tr(kindNames[k])) + " " + std::to_string(kinds[k]);
                    if (k == 3) {
                        seen += " (";
                        bool first = true;
                        for (int c = 0; c < 128; ++c) {
                            if (controllers[c] == 0) continue;
                            seen += (first ? "" : ", ") + std::to_string(c) + ": " + std::to_string(controllers[c]);
                            first = false;
                        }
                        seen += ")";
                    }
                }
                if (!seen.empty()) dim(std::string(tr("Seen:")) + " " + seen);
            } else {
                dim(listenToPlay ? tr("Play port not open: no touches.") : tr("Play port off (View menu): no touches."));
            }
        }
        std::vector<std::string> lines = logLines();
        if (!lines.empty() && ImGui::TreeNode(tr("Detection log"))) {
            for (std::size_t k = lines.size(); k-- > 0;) ImGui::TextDisabled("%s", joue::i18n::message(lines[k]).c_str());
            ImGui::TreePop();
        }
        ImGui::TreePop();
    }
    ImGui::EndChild();
    ImGui::EndChild();
}
