#pragma once
#include <atomic>
#include <functional>
#include <string>
#include <protocomm.h>
#include <protocomm_security.h>
#include <esp_event.h>

// ESP-IDF Protocomm Security2 (SRP6a + AES-GCM), active only after a physical action.
class WifiVoicePairing {
public:
    using Save = std::function<bool(const std::string&,const std::string&,const std::string&,const std::string&)>;
    using Notice = std::function<void(const char*,const std::string&)>;
    void Start(Save save, Notice notice);
    void Begin() { requested_=true; }
private:
    std::atomic<bool> requested_{false}, done_{false};
    std::atomic<unsigned> attempts_{0};
    std::atomic<unsigned long long> expires_{0};
    protocomm_t* pc_=nullptr;
    protocomm_security2_params_t parameters_{};
    esp_event_handler_instance_t event_handler_=nullptr;
    char* salt_=nullptr;
    char* verifier_=nullptr;
    Save save_;
    Notice notice_;
    std::string device_id_;
    bool Active() const;
    bool Open();
    void Close();
    void Run();
    static esp_err_t SaveRequest(uint32_t,const uint8_t*,ssize_t,uint8_t**,ssize_t*,void*);
};
