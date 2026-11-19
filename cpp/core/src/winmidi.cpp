// Windows backend on the multimedia API (winmm), the same API the original Windows editor uses.
#ifdef _WIN32
#include <windows.h>
#include <mmsystem.h>

#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <stdexcept>
#include <thread>

#include "joue/endpoints.hpp"
#include "joue/midiparse.hpp"
#include "virtualcore.hpp"

namespace joue {

namespace {

std::string narrow(const wchar_t* text) {
    int size = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (size <= 1) return {};
    std::string out(static_cast<std::size_t>(size - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text, -1, out.data(), size, nullptr, nullptr);
    return out;
}

constexpr int BufferCount = 4;
constexpr DWORD BufferSize = 4096;

class WinMidiPort : public MidiPort {
public:
    explicit WinMidiPort(const PortPair& pair, bool inputOnly) {
        if (!inputOnly) check(midiOutOpen(&out_, static_cast<UINT>(pair.destination.ref), 0, 0, CALLBACK_NULL), "midiOutOpen");
        try {
            openInput(static_cast<UINT>(pair.source.ref));
        } catch (...) {
            if (out_) midiOutClose(out_);
            throw;
        }
    }

    WinMidiPort(const EndpointInfo& endpoint, bool outputSide) {
        if (outputSide) check(midiOutOpen(&out_, static_cast<UINT>(endpoint.ref), 0, 0, CALLBACK_NULL), "midiOutOpen");
        else openInput(static_cast<UINT>(endpoint.ref));
    }

    ~WinMidiPort() override {
        if (in_) {
            midiInStop(in_);
            midiInReset(in_);
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stopping_ = true;
            }
            wake_.notify_all();
            worker_.join();
            for (int i = 0; i < BufferCount; ++i) midiInUnprepareHeader(in_, &headers_[i], sizeof(MIDIHDR));
            midiInClose(in_);
        }
        if (out_) {
            midiOutReset(out_);
            midiOutClose(out_);
        }
    }

    void sendMessage(const Bytes& message) override {
        if (message.empty()) return;
        if (message.size() <= 3 && message[0] < 0xF0) {
            if (!out_) throw std::runtime_error("This port was opened for listening only.");
            DWORD packed = message[0];
            if (message.size() > 1) packed |= static_cast<DWORD>(message[1]) << 8;
            if (message.size() > 2) packed |= static_cast<DWORD>(message[2]) << 16;
            check(midiOutShortMsg(out_, packed), "midiOutShortMsg");
            return;
        }
        send(message);
    }

    void send(const Bytes& sysex) override {
        if (!out_) throw std::runtime_error("This port was opened for listening only.");
        std::vector<char> data(sysex.begin(), sysex.end());
        MIDIHDR header{};
        header.lpData = data.data();
        header.dwBufferLength = static_cast<DWORD>(data.size());
        header.dwBytesRecorded = static_cast<DWORD>(data.size());
        check(midiOutPrepareHeader(out_, &header, sizeof(MIDIHDR)), "midiOutPrepareHeader");
        MMRESULT result = midiOutLongMsg(out_, &header, sizeof(MIDIHDR));
        if (result == MMSYSERR_NOERROR) {
            for (int i = 0; i < 1000 && !(header.dwFlags & MHDR_DONE); ++i) Sleep(1);
        }
        midiOutUnprepareHeader(out_, &header, sizeof(MIDIHDR));
        check(result, "midiOutLongMsg");
    }

private:
    void openInput(UINT device) {
        check(midiInOpen(&in_, device, reinterpret_cast<DWORD_PTR>(&WinMidiPort::inProc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION), "midiInOpen");
        for (int i = 0; i < BufferCount; ++i) {
            buffers_[i].resize(BufferSize);
            headers_[i] = MIDIHDR{};
            headers_[i].lpData = buffers_[i].data();
            headers_[i].dwBufferLength = BufferSize;
            midiInPrepareHeader(in_, &headers_[i], sizeof(MIDIHDR));
            midiInAddBuffer(in_, &headers_[i], sizeof(MIDIHDR));
        }
        worker_ = std::thread([this] { run(); });
        midiInStart(in_);
    }

    static void check(MMRESULT status, const char* call) {
        if (status != MMSYSERR_NOERROR) throw std::runtime_error(std::string(call) + " failed with code " + std::to_string(status));
    }

    // winmm restricts what a callback may call, so the data is handed to a worker thread that also returns the buffer.
    static void CALLBACK inProc(HMIDIIN, UINT message, DWORD_PTR instance, DWORD_PTR param1, DWORD_PTR) {
        auto* self = reinterpret_cast<WinMidiPort*>(instance);
        if (message == MIM_LONGDATA) {
            auto* header = reinterpret_cast<MIDIHDR*>(param1);
            {
                std::lock_guard<std::mutex> lock(self->mutex_);
                self->pending_.push_back(header);
            }
            self->wake_.notify_one();
        } else if (message == MIM_DATA) {
            // a short channel message: status, then up to two data bytes, packed in one word
            Bytes m{static_cast<std::uint8_t>(param1 & 0xFF), static_cast<std::uint8_t>((param1 >> 8) & 0xFF), static_cast<std::uint8_t>((param1 >> 16) & 0xFF)};
            if (m[0] >= 0xF0) return;
            if ((m[0] & 0xF0) == 0xC0 || (m[0] & 0xF0) == 0xD0) m.pop_back();
            {
                std::lock_guard<std::mutex> lock(self->mutex_);
                self->shorts_.push_back(m);
            }
            self->wake_.notify_one();
        }
    }

    void run() {
        while (true) {
            MIDIHDR* header = nullptr;
            Bytes shortMessage;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                wake_.wait(lock, [this] { return stopping_ || !pending_.empty() || !shorts_.empty(); });
                if (!shorts_.empty()) {
                    shortMessage = shorts_.front();
                    shorts_.pop_front();
                } else if (!pending_.empty()) {
                    header = pending_.front();
                    pending_.pop_front();
                } else {
                    return;
                }
            }
            if (!shortMessage.empty()) {
                deliverAny(shortMessage);
                continue;
            }
            if (header->dwBytesRecorded > 0) {
                for (const Bytes& message : assembler_.feed(reinterpret_cast<const std::uint8_t*>(header->lpData), header->dwBytesRecorded)) deliverAny(message);
            }
            if (!stopping_) midiInAddBuffer(in_, header, sizeof(MIDIHDR));
        }
    }

    HMIDIIN in_ = nullptr;
    HMIDIOUT out_ = nullptr;
    std::vector<char> buffers_[BufferCount];
    MIDIHDR headers_[BufferCount];
    MessageParser assembler_;
    std::thread worker_;
    std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<MIDIHDR*> pending_;
    std::deque<Bytes> shorts_;
    bool stopping_ = false;
};

}  // namespace

std::vector<EndpointInfo> listEndpoints() {
    std::vector<EndpointInfo> result;
    UINT inputs = midiInGetNumDevs();
    for (UINT i = 0; i < inputs; ++i) {
        MIDIINCAPSW caps{};
        if (midiInGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        EndpointInfo info;
        info.ref = i;
        info.isSource = true;
        info.index = static_cast<int>(i);
        info.name = info.displayName = narrow(caps.szPname);
        result.push_back(info);
    }
    UINT outputs = midiOutGetNumDevs();
    for (UINT i = 0; i < outputs; ++i) {
        MIDIOUTCAPSW caps{};
        if (midiOutGetDevCapsW(i, &caps, sizeof(caps)) != MMSYSERR_NOERROR) continue;
        EndpointInfo info;
        info.ref = i;
        info.isSource = false;
        info.index = static_cast<int>(i);
        info.name = info.displayName = narrow(caps.szPname);
        result.push_back(info);
    }
    return result;
}

std::unique_ptr<MidiPort> openPort(const PortPair& pair, bool inputOnly) { return std::make_unique<WinMidiPort>(pair, inputOnly); }

namespace {

// The interface of the teVirtualMIDI driver, which loopMIDI installs, is loaded when it is needed so that nothing has to be installed with
// the program.
using VmPort = void*;
using VmCallback = void(CALLBACK*)(VmPort, LPBYTE, DWORD, DWORD_PTR);
using VmCreate = VmPort(CALLBACK*)(LPCWSTR, VmCallback, DWORD_PTR, DWORD, DWORD);
using VmClose = void(CALLBACK*)(VmPort);
using VmSend = BOOL(CALLBACK*)(VmPort, LPBYTE, DWORD);

struct VmApi {
    HMODULE module = nullptr;
    VmCreate create = nullptr;
    VmClose close = nullptr;
    VmSend send = nullptr;
};

const VmApi& vmApi() {
    static const VmApi api = [] {
        VmApi found;
#ifdef _WIN64
        const wchar_t* names[] = {L"teVirtualMIDI64.dll", L"teVirtualMIDI.dll"};
#else
        const wchar_t* names[] = {L"teVirtualMIDI32.dll", L"teVirtualMIDI.dll"};
#endif
        for (const wchar_t* name : names) {
            HMODULE module = LoadLibraryW(name);
            if (!module) continue;
            auto create = reinterpret_cast<VmCreate>(reinterpret_cast<void*>(GetProcAddress(module, "virtualMIDICreatePortEx2")));
            auto close = reinterpret_cast<VmClose>(reinterpret_cast<void*>(GetProcAddress(module, "virtualMIDIClosePort")));
            auto send = reinterpret_cast<VmSend>(reinterpret_cast<void*>(GetProcAddress(module, "virtualMIDISendData")));
            if (create && close && send) {
                found.module = module;
                found.create = create;
                found.close = close;
                found.send = send;
                break;
            }
            FreeLibrary(module);
        }
        return found;
    }();
    return api;
}

const char* missingDriver() {
    return "To create MIDI ports itself the program uses the driver that loopMIDI installs. Install loopMIDI once (tobias-erichsen.de/software/loopmidi.html); "
           "no port has to be created in it.";
}

class VmCore : public VirtualCore {
public:
    VmCore(const std::string& name, std::string& error) {
        const VmApi& api = vmApi();
        std::wstring wide(static_cast<std::size_t>(MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, nullptr, 0)), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, name.c_str(), -1, wide.data(), static_cast<int>(wide.size()));
        wide.resize(wide.size() ? wide.size() - 1 : 0);
        // 1 and 2: whole messages in both directions; 12: both an input and an output.
        port_ = api.create(wide.c_str(), &VmCore::callback, reinterpret_cast<DWORD_PTR>(this), 65535, 1 | 2 | 12);
        if (!port_) error = "The port \"" + name + "\" could not be created (error " + std::to_string(GetLastError()) + "). Another program may already have a port of that name.";
    }

    ~VmCore() override {
        if (port_) vmApi().close(port_);
    }

    bool ok() const { return port_ != nullptr; }

    void write(const Bytes& message) override {
        Bytes copy = message;
        if (!vmApi().send(port_, copy.data(), static_cast<DWORD>(copy.size()))) throw std::runtime_error("virtualMIDISendData failed (error " + std::to_string(GetLastError()) + ")");
    }

private:
    static void CALLBACK callback(VmPort, LPBYTE data, DWORD length, DWORD_PTR instance) {
        auto* self = reinterpret_cast<VmCore*>(instance);
        try {
            self->received(Bytes(data, data + length));
        } catch (...) {
        }
    }

    VmPort port_ = nullptr;
};

}  // namespace

std::unique_ptr<VirtualCore> makeVirtualCore(const std::string& name, std::string& error) {
    if (!vmApi().create) {
        error = missingDriver();
        return nullptr;
    }
    auto core = std::make_unique<VmCore>(name, error);
    if (!core->ok()) return nullptr;
    return core;
}

bool virtualCoreAvailable(std::string& explanation) {
    if (vmApi().create) return true;
    explanation = missingDriver();
    return false;
}

std::unique_ptr<MidiPort> openOutputPort(const EndpointInfo& destination) { return std::make_unique<WinMidiPort>(destination, true); }

std::unique_ptr<MidiPort> openInputPort(const EndpointInfo& source) { return std::make_unique<WinMidiPort>(source, false); }

}  // namespace joue
#endif  // _WIN32
