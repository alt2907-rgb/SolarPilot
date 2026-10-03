#include "core/WiFiSetup.h"
#include <WiFi.h>
#include <esp_wifi.h>
namespace solarpilot::core {
namespace {
void wipe(String& value) { for (size_t i=0; i<value.length(); ++i) value.setCharAt(i, 0); value = ""; }
String jsonQuote(const String& value) {
  String out = "\"";
  for (size_t i=0; i<value.length(); ++i) {
    const unsigned char c=value[i];
    if (c=='"' || c=='\\') { out+='\\'; out+=char(c); }
    else if (c<32) { char escaped[7]; snprintf(escaped,sizeof(escaped),"\\u%04x",c); out+=escaped; }
    else out+=char(c);
  }
  return out+'"';
}
void radio() { WiFi.setSleep(false); esp_wifi_set_ps(WIFI_PS_NONE); WiFi.setTxPower(WIFI_POWER_8_5dBm); }
}
bool WiFiSetup::start() {
  if (active_) return true;
  char suffix[9]; snprintf(suffix,sizeof(suffix),"%08lx",static_cast<unsigned long>(ESP.getEfuseMac() & 0xFFFFFFFF));
  apName_ = "SolarPilot-" + String(suffix);
  apPassword_ = String(esp_random(),HEX)+String(esp_random(),HEX);
  while (apPassword_.length()<12) apPassword_ += '0';
  WiFi.mode(WIFI_AP_STA);
  if (!WiFi.softAP(apName_.c_str(),apPassword_.c_str(),1,false,2)) { wipe(apPassword_); return false; }
  radio(); active_=true; closeRequested_=false; startedMs_=millis();
  testing_=tested_=scanning_=false; connectedMs_=drops_=0;
  return true;
}
bool WiFiSetup::scan() {
  if (!active_ || testing_ || scanning_) return false;
  WiFi.scanDelete();
  scanning_=WiFi.scanNetworks(true,true)==WIFI_SCAN_RUNNING;
  return scanning_;
}
bool WiFiSetup::test(String ssid, String password) {
  bool hex=true;
  for (size_t i=0; i<password.length(); ++i) if (!isxdigit(static_cast<unsigned char>(password[i]))) hex=false;
  bool clean=true;
  for(size_t i=0;i<ssid.length();++i) if(ssid[i]==0) clean=false;
  for(size_t i=0;i<password.length();++i) if(password[i]==0) clean=false;
  const bool valid=active_ && !testing_ && !scanning_ && clean && ssid.length()>0 && ssid.length()<=32 &&
    (password.isEmpty() || (password.length()>=8 && password.length()<=63) || (password.length()==64 && hex));
  if (!valid) { wipe(password); return false; }
  wipe(password_); ssid_=ssid; password_=password; wipe(password);
  WiFi.disconnect(false,false);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN); WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(ssid_.c_str(),password_.c_str()); radio();
  testStartedMs_=sampleMs_=millis(); connectedMs_=drops_=0;
  testing_=true; tested_=false; wasConnected_=false;
  return true;
}
void WiFiSetup::tick() {
  if (!active_) return;
  const uint32_t now=millis();
  if (scanning_ && WiFi.scanComplete()!=WIFI_SCAN_RUNNING) scanning_=false;
  if (testing_) {
    const bool connected=WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid_;
    uint32_t delta=static_cast<uint32_t>(now-sampleMs_);
    const uint32_t previous=static_cast<uint32_t>(sampleMs_-testStartedMs_);
    const uint32_t remaining=previous<30000U?30000U-previous:0U;
    if(delta>remaining) delta=remaining;
    if (connected && wasConnected_) connectedMs_ += delta;
    if (!connected && wasConnected_) ++drops_;
    sampleMs_=now; wasConnected_=connected;
    if (static_cast<uint32_t>(now-testStartedMs_)>=30000U) { testing_=false; tested_=true; }
  }
  if (static_cast<uint32_t>(now-startedMs_)>=600000U) closeRequested_=true;
}
bool WiFiSetup::canSave() const {
  return active_ && tested_ && !testing_ && connectedMs_>=20000U && drops_==0 &&
    WiFi.status()==WL_CONNECTED && WiFi.SSID()==ssid_;
}
void WiFiSetup::close() {
  WiFi.scanDelete(); WiFi.softAPdisconnect(true);
  active_=scanning_=testing_=tested_=closeRequested_=false;
  wipe(password_); wipe(apPassword_); ssid_="";
}
String WiFiSetup::statusJson() const {
  wifi_mode_t mode=WIFI_MODE_NULL;
  const bool modeRead=esp_wifi_get_mode(&mode)==ESP_OK;
  const bool apEnabled=modeRead && (mode==WIFI_MODE_AP || mode==WIFI_MODE_APSTA);
  int8_t power=0; const bool powerRead=esp_wifi_get_max_tx_power(&power)==ESP_OK;
  const uint32_t elapsed=static_cast<uint32_t>(millis()-startedMs_);
  const uint32_t remaining=active_ && elapsed<600000U?(600000U-elapsed)/1000U:0U;
  return "{\"active\":"+String(active_?"true":"false")+",\"scanning\":"+String(scanning_?"true":"false")+
    ",\"ap_enabled\":"+String(apEnabled?"true":"false")+",\"remaining_seconds\":"+String(remaining)+
    ",\"ap_clients\":"+String(apEnabled?WiFi.softAPgetStationNum():0)+
    ",\"radio_power_quarter_dbm\":"+String(powerRead?int(power):-1)+
    ",\"testing\":"+String(testing_?"true":"false")+",\"tested\":"+String(tested_?"true":"false")+
    ",\"can_save\":"+String(canSave()?"true":"false")+",\"connected_seconds\":"+String(connectedMs_/1000)+
    ",\"drops\":"+String(drops_)+",\"ap_name\":"+jsonQuote(active_?apName_:String())+
    ",\"ap_key\":"+jsonQuote(active_?apPassword_:String())+",\"ip\":"+
    jsonQuote(WiFi.status()==WL_CONNECTED?WiFi.localIP().toString():String())+"}";
}
String WiFiSetup::networksJson() {
  if (scanning_) return "{\"scanning\":true,\"networks\":[]}";
  const int count=WiFi.scanComplete();
  String out="{\"scanning\":false,\"networks\":[";
  unsigned included=0;
  for(int i=0; i<count && included<24; ++i) {
    const String name=WiFi.SSID(i); if(name.isEmpty()) continue;
    if(included++) out+=',';
    out+="{\"ssid\":"+jsonQuote(name)+",\"rssi\":"+String(WiFi.RSSI(i))+"}";
  }
  return out+"]}";
}
}
