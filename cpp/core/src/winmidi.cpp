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
        UINT inDevice = pair.source.ref, outDevice = pair.destination.ref;
        if (!inputOnly) check(midiOutOpen(&out_, outDevice, 0, 0, CALLBACK_NULL), "midiOutOpen");
        MMRESULT result = midiInOpen(&in_, inDevice, reinterpret_cast<DWORD_PTR>(&WinMidiPort::inProc), reinterpret_cast<DWORD_PTR>(this), CALLBACK_FUNCTION);
        if (result != MMSYSERR_NOERROR) {
            if (out_) midiOutClose(out_);
            check(result, "midiInOpen");
        }
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

    ~WinMidiPort() override {
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
        if (out_) {
            midiOutReset(out_);
            midiOutClose(out_);
        }
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

}  // namespace joue
#endif  // _WIN32
