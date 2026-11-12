#pragma once
#include <atomic>
#include <functional>
#include <string>
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "wifi_voice_pairing.h"

// One paired, certificate-verified TLS connection. Only this task touches TLS.
class WifiVoiceLink {
public:
    void Start(std::function<void(const std::string&)> received, std::function<void()> disconnected, WifiVoicePairing::Notice notice);
    void BeginPairing() { pairing_.Begin(); }
    bool Connected() const { return connected_.load(); }
    bool Send(const std::string& frame);
    bool Configure(const std::string& certificate, const std::string& token,
                   const std::string& identity, const std::string& host);
private:
    WifiVoicePairing pairing_;
    std::atomic<bool> connected_{false};
    std::atomic<bool> reload_{false};
    QueueHandle_t outgoing_=nullptr;
    std::function<void(const std::string&)> received_;
    std::function<void()> disconnected_;
    void Run();
    std::string Discover(const std::string& identity);
};
