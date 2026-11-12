#include "wifi_voice_link.h"
#include "settings.h"
#include <cJSON.h>
#include <esp_tls.h>
#include <esp_netif.h>
#include <esp_timer.h>
#include <lwip/sockets.h>
#include <fcntl.h>
#include <cstring>
#include <memory>
#include <nvs.h>

namespace {
constexpr int kPort=45831, kDiscoveryPort=45832;
unsigned long long Millis() { return esp_timer_get_time()/1000; }
bool Hex(const std::string& value, size_t length) {
    return value.size()==length && value.find_first_not_of("0123456789abcdef")==std::string::npos;
}
bool WriteAll(esp_tls_t* tls, const std::string& value) {
    size_t offset=0; auto deadline=Millis()+2500;
    while (offset<value.size() && Millis()<deadline) {
        auto count=esp_tls_conn_write(tls,value.data()+offset,value.size()-offset);
        if (count>0) offset+=count;
        else if (count==ESP_TLS_ERR_SSL_WANT_READ || count==ESP_TLS_ERR_SSL_WANT_WRITE) vTaskDelay(pdMS_TO_TICKS(10));
        else return false;
    }
    return offset==value.size();
}
std::string Field(cJSON* root,const char* key) {
    auto value=cJSON_GetObjectItemCaseSensitive(root,key);
    return cJSON_IsString(value) ? value->valuestring : "";
}
}
void WifiVoiceLink::Start(std::function<void(const std::string&)> received, std::function<void()> disconnected, WifiVoicePairing::Notice notice) {
    received_=received; disconnected_=disconnected;
    pairing_.Start([this](const std::string& cert,const std::string& token,const std::string& identity,const std::string& host) {
        return Configure(cert,token,identity,host);
    },notice);
    outgoing_=xQueueCreate(16,sizeof(std::string*));
    configASSERT(outgoing_);
    BaseType_t result=xTaskCreate([](void* context) { static_cast<WifiVoiceLink*>(context)->Run(); },
        "voice_wifi",12288,this,2,nullptr);
    configASSERT(result==pdPASS);
}
bool WifiVoiceLink::Configure(const std::string& certificate,const std::string& token,
    const std::string& identity,const std::string& host) {
    in_addr address{};
    if (certificate.size()<500 || certificate.size()>2000 || certificate.rfind("-----BEGIN CERTIFICATE-----",0)!=0 ||
        certificate.find("PRIVATE")!=std::string::npos || !Hex(token,64) || !Hex(identity,32) ||
        inet_pton(AF_INET,host.c_str(),&address)!=1) return false;
    auto root=cJSON_CreateObject();
    cJSON_AddStringToObject(root,"cert",certificate.c_str()); cJSON_AddStringToObject(root,"token",token.c_str());
    cJSON_AddStringToObject(root,"identity",identity.c_str()); cJSON_AddStringToObject(root,"host",host.c_str());
    char* encoded=cJSON_PrintUnformatted(root);
    nvs_handle_t handle=0;
    esp_err_t error=nvs_open("voice_wifi",NVS_READWRITE,&handle);
    if (error==ESP_OK) {
        error=encoded ? nvs_set_str(handle,"pair",encoded) : ESP_ERR_NO_MEM;
        if (error==ESP_OK) error=nvs_commit(handle);
        nvs_close(handle);
    }
    cJSON_free(encoded); cJSON_Delete(root);
    if (error==ESP_OK) reload_=true;
    return error==ESP_OK;
}
bool WifiVoiceLink::Send(const std::string& frame) {
    if (!connected_) return false;
    auto message=new std::string(frame+"\n");
    if (xQueueSend(outgoing_,&message,0)!=pdTRUE) { delete message; connected_=false; return false; }
    return true;
}
std::string WifiVoiceLink::Discover(const std::string& identity) {
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if (fd<0) return "";
    int yes=1; setsockopt(fd,SOL_SOCKET,SO_BROADCAST,&yes,sizeof(yes));
    timeval timeout{1,0}; setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    sockaddr_in target{}; target.sin_family=AF_INET; target.sin_port=htons(kDiscoveryPort); target.sin_addr.s_addr=INADDR_BROADCAST;
    std::string request="CVI-DISCOVER/1 "+identity;
    sendto(fd,request.data(),request.size(),0,reinterpret_cast<sockaddr*>(&target),sizeof(target));
    char response[128]{}; sockaddr_in source{}; socklen_t length=sizeof(source);
    int count=recvfrom(fd,response,sizeof(response),0,reinterpret_cast<sockaddr*>(&source),&length);
    close(fd);
    if (count<=0 || std::string(response,count)!="CVI-HOST/1 "+identity+" "+std::to_string(kPort)) return "";
    char address[INET_ADDRSTRLEN]{}; inet_ntop(AF_INET,&source.sin_addr,address,sizeof(address));
    return address;
}
void WifiVoiceLink::Run() {
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        auto netif=esp_netif_get_handle_from_ifkey("WIFI_STA_DEF"); esp_netif_ip_info_t ip{};
        if (!netif || esp_netif_get_ip_info(netif,&ip)!=ESP_OK || ip.ip.addr==0) continue;
        std::string pair;
        { Settings settings("voice_wifi"); pair=settings.GetString("pair"); }
        cJSON* root=cJSON_Parse(pair.c_str());
        if (!root) continue;
        auto certificate=Field(root,"cert"), token=Field(root,"token"), identity=Field(root,"identity"), host=Field(root,"host");
        cJSON_Delete(root);
        if (certificate.empty() || !Hex(token,64) || !Hex(identity,32)) continue;
        auto discovered=Discover(identity); if (!discovered.empty()) host=discovered;
        esp_tls_cfg_t config{};
        config.cacert_buf=reinterpret_cast<const unsigned char*>(certificate.c_str());
        config.cacert_bytes=certificate.size()+1;
        config.skip_common_name=true; // Trust the USB-paired self-signed certificate, independent of DHCP address.
        config.timeout_ms=5000;
        auto tls=esp_tls_init();
        if (!tls) continue;
        reload_=false;
        if (esp_tls_conn_new_sync(host.c_str(),host.size(),kPort,&config,tls)!=1) { esp_tls_conn_destroy(tls); continue; }
        int fd=-1; esp_tls_get_conn_sockfd(tls,&fd);
        fcntl(fd,F_SETFL,fcntl(fd,F_GETFL,0)|O_NONBLOCK);
        bool valid=WriteAll(tls,"@CVI-AUTH1 "+token+"\n");
        std::string line;
        auto deadline=Millis()+5000;
        while (valid && Millis()<deadline && line.find('\n')==std::string::npos) {
            char byte; int count=esp_tls_conn_read(tls,&byte,1);
            if (count==1) { line.push_back(byte); if (line.size()>40) valid=false; }
            else if (count==ESP_TLS_ERR_SSL_WANT_READ || count==ESP_TLS_ERR_SSL_WANT_WRITE) vTaskDelay(pdMS_TO_TICKS(10));
            else valid=false;
        }
        valid=valid && line=="@CVI-AUTH1 OK\n";
        connected_=valid;
        bool authenticated=valid;
        line.clear();
        while (valid && connected_ && !reload_) {
            std::string* message=nullptr;
            while (xQueueReceive(outgoing_,&message,0)==pdTRUE) {
                std::unique_ptr<std::string> owned(message);
                if (!WriteAll(tls,*owned)) { valid=false; break; }
            }
            if (!valid) break;
            char bytes[512]; int count=esp_tls_conn_read(tls,bytes,sizeof(bytes));
            if (count>0) {
                for (int i=0;i<count;++i) {
                    if (bytes[i]=='\n') {
                        if (line.rfind("@CVI1 ",0)==0) received_(line);
                        line.clear();
                    } else if (bytes[i]!='\r') {
                        if (line.size()<4096) line.push_back(bytes[i]); else { valid=false; break; }
                    }
                }
            } else if (count!=ESP_TLS_ERR_SSL_WANT_READ && count!=ESP_TLS_ERR_SSL_WANT_WRITE) valid=false;
            vTaskDelay(pdMS_TO_TICKS(10));
        }
        connected_=false;
        esp_tls_conn_destroy(tls);
        std::string* old=nullptr; while (xQueueReceive(outgoing_,&old,0)==pdTRUE) delete old;
        if (authenticated) disconnected_();
    }
}
