#pragma once
#include "Arduino.h"
#include <string>

constexpr int WIFI_STA = 1, WL_CONNECTED = 3;
class IPAddress {
public:
    IPAddress() = default;
    IPAddress(int a, int b, int c, int d);
    bool fromString(const char* text);
    bool operator==(const IPAddress& other) const { return value_ == other.value_; }
private:
    std::string value_;
};
struct FakeWiFi {
    void mode(int) {}
    void setAutoReconnect(bool) {}
    void disconnect();
    void begin(const char*, const char*);
    int status();
};
extern FakeWiFi WiFi;
class WiFiClient {
public:
    int connect(IPAddress, uint16_t, int32_t);
    void setNoDelay(bool) {}
    void stop();
    uint8_t connected();
    int available();
    int read();
    int fd() const { return 42; }
};
