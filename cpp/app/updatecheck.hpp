#pragma once
#include <memory>
#include <mutex>
#include <string>

class UpdateCheck {
public:
    UpdateCheck() : shared_(std::make_shared<Shared>()) {}
    void start(const std::string& repository);
    bool running();
    bool takeResult(bool& ok, std::string& tag, std::string& url);

private:
    struct Shared {
        std::mutex mutex;
        bool running = false;
        bool ready = false;
        bool ok = false;
        std::string tag, url;
    };
    std::shared_ptr<Shared> shared_;
};

void openExternal(const std::string& target);
std::string executableDirectory();
