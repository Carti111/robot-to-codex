#include "wifi_voice_pairing.h"
#include <protocomm_httpd.h>
#include <protocomm_security2.h>
#include <esp_srp.h>
#include <esp_timer.h>
#include <esp_random.h>
#include <esp_mac.h>
#include <esp_netif.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <lwip/sockets.h>
#include <cJSON.h>
#include <cstring>
#include <cstdio>

namespace {
unsigned long long PairMillis() {return esp_timer_get_time()/1000;}
std::string PairField(cJSON* root,const char* name) {
    auto value=cJSON_GetObjectItemCaseSensitive(root,name);
    return cJSON_IsString(value) ? value->valuestring : "";
}
}
void WifiVoicePairing::Start(Save save, Notice notice) {
    save_=save; notice_=notice;
    uint8_t mac[6]; esp_read_mac(mac,ESP_MAC_WIFI_STA);
    char id[13]; snprintf(id,sizeof(id),"%02x%02x%02x%02x%02x%02x",mac[0],mac[1],mac[2],mac[3],mac[4],mac[5]);
    device_id_=id;
    ESP_ERROR_CHECK(esp_event_handler_instance_register(PROTOCOMM_SECURITY_SESSION_EVENT,ESP_EVENT_ANY_ID,
        [](void* arg,esp_event_base_t,int32_t event,void*) {
            auto self=static_cast<WifiVoicePairing*>(arg);
            if(self->Active() && (event==PROTOCOMM_SECURITY_SESSION_CREDENTIALS_MISMATCH ||
                event==PROTOCOMM_SECURITY_SESSION_INVALID_SECURITY_PARAMS)) ++self->attempts_;
        },this,&event_handler_));
    configASSERT(xTaskCreate([](void* arg){static_cast<WifiVoicePairing*>(arg)->Run();},"voice_pair",8192,this,2,nullptr)==pdPASS);
}
bool WifiVoicePairing::Active() const {return expires_>PairMillis() && attempts_<5 && !done_;}
bool WifiVoicePairing::Open() {
    Close(); attempts_=0; done_=false;
    unsigned value; do {value=esp_random();} while(value>=4294000000U);
    char pin[7]; snprintf(pin,sizeof(pin),"%06u",value%1000000U);
    int verifier_length=0;
    if(esp_srp_gen_salt_verifier("sparkbot",8,pin,6,&salt_,16,&verifier_,&verifier_length)!=ESP_OK) return false;
    parameters_.salt=salt_;parameters_.salt_len=16;parameters_.verifier=verifier_;parameters_.verifier_len=verifier_length;
    pc_=protocomm_new();
    if(!pc_) {Close();return false;}
    protocomm_httpd_config_t config{};
    config.data.config.port=45834;config.data.config.stack_size=8192;config.data.config.task_priority=3;
    if(protocomm_httpd_start(pc_,&config)!=ESP_OK ||
        protocomm_set_security(pc_,"pair-session",&protocomm_security2,&parameters_)!=ESP_OK ||
        protocomm_add_endpoint(pc_,"pair-config",SaveRequest,this)!=ESP_OK) {Close();return false;}
    expires_=PairMillis()+120000;
    notice_("pairing",pin); // Screen only. Never print the temporary code or verifier.
    memset(pin,0,sizeof(pin));
    return true;
}
void WifiVoicePairing::Close() {
    expires_=0;
    if(pc_) {protocomm_httpd_stop(pc_);protocomm_delete(pc_);pc_=nullptr;}
    if(salt_) {free(salt_);salt_=nullptr;}
    if(verifier_) {free(verifier_);verifier_=nullptr;}
}
esp_err_t WifiVoicePairing::SaveRequest(uint32_t,const uint8_t* input,ssize_t length,uint8_t** output,ssize_t* output_length,void* context) {
    auto self=static_cast<WifiVoicePairing*>(context);
    if(!self->Active() || length<=0 || length>3000) return ESP_ERR_INVALID_STATE;
    auto root=cJSON_ParseWithLength(reinterpret_cast<const char*>(input),length);
    if(!root) return ESP_ERR_INVALID_ARG;
    bool ok=self->save_(PairField(root,"cert"),PairField(root,"token"),PairField(root,"identity"),PairField(root,"host"));
    cJSON_Delete(root);
    const char* reply=ok ? "{\"ok\":true}" : "{\"ok\":false}";
    *output_length=strlen(reply);*output=static_cast<uint8_t*>(malloc(*output_length));
    if(!*output) return ESP_ERR_NO_MEM;
    memcpy(*output,reply,*output_length);
    if(ok) {self->done_=true;self->expires_=PairMillis()+1500;}
    return ESP_OK;
}
void WifiVoicePairing::Run() {
    while(!esp_netif_get_handle_from_ifkey("WIFI_STA_DEF")) vTaskDelay(pdMS_TO_TICKS(500));
    int fd=socket(AF_INET,SOCK_DGRAM,IPPROTO_UDP);
    if(fd<0) {notice_("pair_error","");vTaskDelete(nullptr);return;}
    sockaddr_in local{};local.sin_family=AF_INET;local.sin_addr.s_addr=INADDR_ANY;local.sin_port=htons(45833);
    timeval timeout{0,200000};setsockopt(fd,SOL_SOCKET,SO_RCVTIMEO,&timeout,sizeof(timeout));
    if(bind(fd,reinterpret_cast<sockaddr*>(&local),sizeof(local))!=0) {close(fd);notice_("pair_error","");vTaskDelete(nullptr);return;}
    while(true) {
        if(requested_.exchange(false) && !Open()) notice_("pair_error","");
        if(pc_ && (expires_<=PairMillis() || attempts_>=5)) {
            bool success=done_;bool locked=attempts_>=5;Close();
            notice_(success?"paired":locked?"pair_locked":"pair_expired","");
        }
        char request[64];sockaddr_in source{};socklen_t source_length=sizeof(source);
        auto count=recvfrom(fd,request,sizeof(request),0,reinterpret_cast<sockaddr*>(&source),&source_length);
        if(count!=10 || std::string(request,count)!="CVI-PAIR/1" || !Active()) continue;
        const auto deadline=expires_.load();
        const auto now=PairMillis();
        if(deadline<=now) continue;
        const unsigned remaining=static_cast<unsigned>((deadline-now)/1000);
        char response[256];
        snprintf(response,sizeof(response),"{\"v\":1,\"name\":\"ESP-SparkBot\",\"device\":\"%s\",\"port\":45834,\"security\":2,\"patch\":1,\"remaining\":%u}",device_id_.c_str(),remaining);
        sendto(fd,response,strlen(response),0,reinterpret_cast<sockaddr*>(&source),source_length);
    }
}
