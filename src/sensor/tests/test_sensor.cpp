// PlatformIO incluye esta carpeta; las pruebas solo se activan en el host.
#ifdef SENSOR_HOST_TEST
#include "SensorClient.h"
#include <lwip/sockets.h>
#include <algorithm>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <cerrno>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

namespace fake {
uint32_t now;
int wifi, wifiAttempts, tcpAttempts;
bool connected, connectSucceeds;
size_t writeLimit;
int sendError;
std::deque<unsigned char> incoming;
std::vector<std::string> sessions;
std::string logs;
std::map<int, int> modes;
unsigned long pulse, pulseTimeout;
int pulsePin, pulses;
void reset() {
    now = 0; wifi = 0; wifiAttempts = tcpAttempts = 0;
    connected = false; connectSucceeds = true;
    writeLimit = 1000; sendError = 0; incoming.clear(); sessions.clear(); logs.clear();
    modes.clear(); pulse = 1450; pulseTimeout = 0; pulsePin = -1; pulses = 0;
}
void receive(const std::string& text) {
    for (unsigned char c : text) incoming.push_back(c);
}
}
FakeSerial Serial;
FakeWiFi WiFi;
unsigned long millis() { return fake::now; }
void delay(unsigned long ms) { fake::now += ms; }
void delayMicroseconds(unsigned int) {}
void pinMode(uint8_t pin, uint8_t mode) { fake::modes[pin] = mode; }
void digitalWrite(uint8_t, uint8_t) {}
unsigned long pulseIn(uint8_t pin, uint8_t, unsigned long timeout) {
    fake::pulsePin = pin; fake::pulseTimeout = timeout; ++fake::pulses;
    return fake::pulse;
}
void FakeSerial::println(const char* text) { fake::logs += std::string(text) + "\n"; }
int FakeSerial::printf(const char* format, ...) {
    char buffer[512]; va_list args; va_start(args, format);
    int count = vsnprintf(buffer, sizeof(buffer), format, args); va_end(args);
    fake::logs += buffer; return count;
}
IPAddress::IPAddress(int a, int b, int c, int d)
    : value_(std::to_string(a)+"."+std::to_string(b)+"."+std::to_string(c)+"."+std::to_string(d)) {}
bool IPAddress::fromString(const char* text) {
    int a, b, c, d; char extra;
    if (sscanf(text, "%d.%d.%d.%d%c", &a, &b, &c, &d, &extra) != 4) return false;
    if (a<0 || a>255 || b<0 || b>255 || c<0 || c>255 || d<0 || d>255) return false;
    value_ = text; return true;
}
void FakeWiFi::disconnect() { fake::wifi = 0; }
void FakeWiFi::begin(const char*, const char*) { ++fake::wifiAttempts; }
int FakeWiFi::status() { return fake::wifi; }
int WiFiClient::connect(IPAddress, uint16_t port, int32_t timeout) {
    if (port != 8080 || timeout != 100) throw std::runtime_error("Destino/timeout incorrecto");
    ++fake::tcpAttempts;
    fake::connected = fake::connectSucceeds;
    if (fake::connected) fake::sessions.emplace_back();
    return fake::connected;
}
void WiFiClient::stop() { fake::connected = false; fake::incoming.clear(); }
uint8_t WiFiClient::connected() { return fake::connected; }
int WiFiClient::available() { return static_cast<int>(fake::incoming.size()); }
int WiFiClient::read() {
    if (fake::incoming.empty()) return -1;
    int byte = fake::incoming.front(); fake::incoming.pop_front(); return byte;
}
int send(int, const void* data, size_t length, int flags) {
    if (flags != MSG_DONTWAIT) throw std::runtime_error("Envio bloqueante");
    if (fake::sendError) { errno = fake::sendError; return -1; }
    size_t count = std::min(length, fake::writeLimit);
    fake::sessions.back().append(static_cast<const char*>(data), count);
    return static_cast<int>(count);
}

using Status = UltrasonicSensor::Status;
void check(bool result) { if (!result) throw std::runtime_error("Verificacion fallida"); }
SensorClient makeClient() { return SensorClient("test", "test-password", "192.0.2.1"); }
void connect(SensorClient& client) {
    client.begin(); fake::wifi = WL_CONNECTED; client.update();
}
void identify(SensorClient& client) {
    connect(client); fake::receive("OK\n"); client.update();
}
UltrasonicSensor::Measurement valid(float distance = 25.5f) { return {Status::Ok, distance, 1479}; }
void setup();
void loop();

int main() {
    const std::vector<std::pair<std::string, std::function<void()>>> tests = {
        {"medicion original, GPIO y timeout", [] {
            UltrasonicSensor sensor(32,35); sensor.begin();
            check(fake::modes[32] == OUTPUT && fake::modes[35] == INPUT);
            auto m = sensor.measure(); check(m.status == Status::Ok && m.distanceCm == 25.0f);
            check(fake::pulsePin == 35 && fake::pulseTimeout == 30000);
            fake::pulse=0; m=sensor.measure(); check(m.status == Status::EchoTimeout && std::isnan(m.distanceCm));
            fake::pulse=58; check(sensor.measure().status == Status::OutOfRange);
        }},
        {"main mantiene lectura local cada 500 ms", [] {
            setup(); check(fake::modes[32] == OUTPUT && fake::modes[35] == INPUT);
            fake::now=499; loop(); check(fake::pulses==0);
            fake::now=500; loop(); check(fake::pulses==1);
            fake::now=999; loop(); check(fake::pulses==1);
            fake::now=1000; loop(); check(fake::pulses==2);
        }},
        {"no enviar antes de OK; OK fragmentado", [] {
            auto c=makeClient(); connect(c);
            check(fake::sessions.back()=="HELLO SENSOR\n");
            check(!c.sendMeasurement(valid())); fake::receive("O"); c.update();
            check(!c.sendMeasurement(valid())); fake::receive("K\n"); c.update();
            check(c.sendMeasurement(valid()));
            check(fake::sessions.back()=="HELLO SENSOR\nDIST 25.50\n");
            check(fake::logs.find("test-password")==std::string::npos);
        }},
        {"formatos, limites y mediciones invalidas", [] {
            auto c=makeClient(); identify(c);
            for(float d : {2.f,10.f,19.99f,20.f,30.f,40.f,40.01f,50.f,400.f}) check(c.sendMeasurement(valid(d)));
            for(float d : {1.99f,400.01f,std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}) check(c.sendMeasurement(valid(d)));
            check(c.sendMeasurement({Status::EchoTimeout,NAN,0}));
            check(c.sendMeasurement({Status::OutOfRange,NAN,58}));
            check(fake::sessions.back()=="HELLO SENSOR\nDIST 2.00\nDIST 10.00\nDIST 19.99\nDIST 20.00\nDIST 30.00\nDIST 40.00\nDIST 40.01\nDIST 50.00\nDIST 400.00\nINVALID\nINVALID\nINVALID\nINVALID\nINVALID\nINVALID\n");
        }},
        {"reconexion TCP a 2 s sin lecturas antiguas", [] {
            auto c=makeClient(); identify(c); fake::connected=false; c.update();
            check(!c.sendMeasurement(valid(10))); fake::now=1999; c.update(); check(fake::tcpAttempts==1);
            fake::now=2000; c.update(); check(fake::tcpAttempts==2);
            check(!c.sendMeasurement(valid(20))); fake::receive("OK\n"); c.update();
            check(fake::sessions.back()=="HELLO SENSOR\n"); c.sendMeasurement(valid(50));
            check(fake::sessions.back()=="HELLO SENSOR\nDIST 50.00\n");
        }},
        {"reintento de conexion fallida", [] {
            auto c=makeClient(); fake::connectSucceeds=false; connect(c);
            fake::now=1999; c.update(); check(fake::tcpAttempts==1);
            fake::now=2000; c.update(); check(fake::tcpAttempts==2);
        }},
        {"perdida WiFi y nueva identificacion", [] {
            auto c=makeClient(); identify(c); fake::wifi=0; c.update(); check(!fake::connected);
            fake::now=1999; c.update(); check(fake::wifiAttempts==1);
            fake::now=2000; c.update(); check(fake::wifiAttempts==2);
            fake::wifi=WL_CONNECTED; c.update(); check(fake::sessions.back()=="HELLO SENSOR\n");
            check(!c.sendMeasurement(valid()));
        }},
        {"timeout OK a 5 s", [] {
            auto c=makeClient(); connect(c); fake::receive("O"); c.update();
            fake::now=4999; c.update(); check(fake::connected);
            fake::now=5000; c.update(); check(!fake::connected);
        }},
        {"ERROR y respuestas fuera del contrato", [] {
            for(const std::string reply : {"ERROR DUPLICATE\n", "ok\n", " OK\n", "OK\r\n", "\n", "OK\nERROR FORMAT\n"}) {
                fake::reset(); auto c=makeClient(); connect(c); fake::receive(reply); c.update();
                check(!fake::connected); check(!c.sendMeasurement(valid()));
            }
        }},
        {"limite de linea y bytes no ASCII", [] {
            for(const auto& reply : {std::string(129,'A'),std::string(1,static_cast<char>(0xff)),std::string(1,'\0')}) {
                fake::reset(); auto c=makeClient(); connect(c); fake::receive(reply); c.update();
                check(!fake::connected);
            }
        }},
        {"escrituras parciales conservan orden", [] {
            auto c=makeClient(); fake::writeLimit=2; connect(c); fake::receive("OK\n");
            for(int i=0;i<10;++i) c.update();
            check(c.sendMeasurement(valid())); for(int i=0;i<10;++i)c.update();
            check(fake::sessions.back()=="HELLO SENSOR\nDIST 25.50\n");
        }},
        {"envio atascado se descarta al reconectar", [] {
            auto c=makeClient(); identify(c); fake::sendError=EAGAIN; c.sendMeasurement(valid(10));
            fake::now=199; c.update(); check(fake::connected);
            fake::now=200; c.update(); check(!fake::connected);
            fake::sendError=0; fake::now=2200; c.update(); fake::receive("OK\n"); c.update();
            check(fake::sessions.back()=="HELLO SENSOR\n");
        }},
        {"error fatal de escritura", [] {
            auto c=makeClient(); identify(c); fake::sendError=EPIPE;
            check(!c.sendMeasurement(valid())); check(!fake::connected);
        }},
        {"reintentos soportan desbordamiento millis", [] {
            fake::now=UINT32_MAX-999; auto c=makeClient(); fake::connectSucceeds=false; connect(c);
            fake::now+=1999; c.update(); check(fake::tcpAttempts==1);
            fake::now+=1; c.update(); check(fake::tcpAttempts==2);
        }},
        {"configuracion vacia deshabilita red", [] {
            SensorClient c("", "", "0.0.0.0"); c.begin(); c.update();
            check(fake::wifiAttempts==0 && fake::tcpAttempts==0);
        }}
    };
    int failures=0;
    for(const auto& test:tests) {
        fake::reset();
        try { test.second(); std::cout<<"OK: "<<test.first<<'\n'; }
        catch(const std::exception& e) { ++failures; std::cerr<<"FAIL: "<<test.first<<": "<<e.what()<<'\n'; }
    }
    std::cout<<tests.size()<<" pruebas, "<<failures<<" fallos\n";
    return failures ? 1 : 0;
}
#endif
