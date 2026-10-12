// PlatformIO incluye esta carpeta; las pruebas solo se activan en el host.
#ifdef ACTUATOR_HOST_TEST
#include "ActuatorClient.h"
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
std::map<int, int> modes, levels;
unsigned long pulse, pulseTimeout;
int pulsePin, pulses;
void reset() {
    now = 0; wifi = 0; wifiAttempts = tcpAttempts = 0;
    connected = false; connectSucceeds = true;
    writeLimit = 1000; sendError = 0; incoming.clear(); sessions.clear(); logs.clear();
    modes.clear(); levels.clear(); pulse = 1450; pulseTimeout = 0; pulsePin = -1; pulses = 0;
}
void receive(const std::string& text) {
    for (unsigned char c : text) incoming.push_back(c);
}
}
FakeSerial Serial;
FakeWiFi WiFi;
uint32_t millis() { return fake::now; }
void delay(unsigned long ms) { fake::now += ms; }
void delayMicroseconds(unsigned int) {}
void pinMode(uint8_t pin, uint8_t mode) { fake::modes[pin] = mode; }
void digitalWrite(uint8_t pin, uint8_t value) { fake::levels[pin] = value; }
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

void check(bool result) { if (!result) throw std::runtime_error("Verificacion fallida"); }
void lights(int red, int yellow, int green) {
    check(fake::levels[cfg::PIN_RED]==red && fake::levels[cfg::PIN_YELLOW]==yellow && fake::levels[cfg::PIN_GREEN]==green);
}
struct Fixture {
    ActuatorManager manager;
    ActuatorClient client;
    Fixture() : client(manager,"test","test-password","192.0.2.1") { manager.begin(); }
    void connect() { client.begin(); fake::wifi=WL_CONNECTED; client.update(); }
    void ready() { connect(); receive("OK\n"); }
    void receive(const std::string& data) { fake::receive(data); client.update(); manager.update(); }
    void tick(uint32_t now) { fake::now=now; client.update(); manager.update(); }
};
void setup();
void loop();

int main() {
    const std::vector<std::pair<std::string,std::function<void()>>> tests = {
        {"parser exacto de cuatro comandos", [] {
            check(CommandParser::parse("LED RED BLINK")==Command::RED_BLINK);
            check(CommandParser::parse("LED YELLOW ON")==Command::YELLOW_ON);
            check(CommandParser::parse("LED GREEN ON")==Command::GREEN_ON);
            check(CommandParser::parse("LED ALL OFF")==Command::ALL_OFF);
            check(std::string(CommandParser::toText(Command::ALL_OFF))=="LED ALL OFF");
            for(const char* text : {"RED_BLINK","led red blink"," LED RED BLINK","LED RED BLINK ","LED  RED BLINK","LED ALL OFF\r","","OK"})
                check(CommandParser::parse(text)==Command::INVALID);
            check(CommandParser::parse(nullptr)==Command::INVALID);
        }},
        {"arranque y GPIO del cableado actual", [] {
            Fixture f; lights(0,0,0);
            check(cfg::PIN_RED==4 && cfg::PIN_YELLOW==2 && cfg::PIN_GREEN==15);
            check(fake::modes[4]==OUTPUT && fake::modes[2]==OUTPUT && fake::modes[15]==OUTPUT);
            f.connect(); lights(0,0,0);
        }},
        {"estados exclusivos y ALL OFF", [] {
            Fixture f; f.ready();
            f.receive("LED RED BLINK\n"); lights(1,0,0);
            f.receive("LED YELLOW ON\n"); lights(0,1,0);
            f.receive("LED GREEN ON\n"); lights(0,0,1);
            f.receive("LED ALL OFF\n"); lights(0,0,0);
            f.tick(1000); lights(0,0,0);
        }},
        {"parpadeo 500 ms sin reinicio por repeticion", [] {
            Fixture f; f.ready(); f.receive("LED RED BLINK\n"); lights(1,0,0);
            f.tick(400); f.receive("LED RED BLINK\n"); lights(1,0,0);
            f.tick(499); lights(1,0,0); f.tick(500); lights(0,0,0);
            f.tick(900); f.receive("LED RED BLINK\n"); lights(0,0,0);
            f.tick(999); lights(0,0,0); f.tick(1000); lights(1,0,0);
        }},
        {"comando previo a OK no activa LEDs", [] {
            Fixture f; f.connect(); f.receive("LED GREEN ON\nOK\nLED RED BLINK\n");
            lights(0,0,0); check(!fake::connected);
            check(fake::sessions.back()=="HELLO ACTUATOR\n");
        }},
        {"OK y comandos fragmentados", [] {
            Fixture f; f.connect(); f.receive("O"); lights(0,0,0);
            f.receive("K\nLED GRE"); lights(0,0,0);
            f.receive("EN ON"); lights(0,0,0); f.receive("\n"); lights(0,0,1);
        }},
        {"varias lineas juntas y ausencia de ACK", [] {
            Fixture f; f.connect(); f.receive("OK\nLED ALL OFF\nLED YELLOW ON\nLED GREEN ON\n");
            lights(0,0,1); f.receive("FOO\n"); lights(0,0,0);
            check(fake::sessions.back()=="HELLO ACTUATOR\n");
            check(fake::logs.find("test-password")==std::string::npos);
        }},
        {"timeout exacto a 3 segundos", [] {
            Fixture f; f.ready(); f.receive("LED GREEN ON\n");
            f.tick(2999); lights(0,0,1); f.tick(3000); lights(0,0,0);
            check(!fake::connected);
        }},
        {"solo comandos validos renuevan timeout", [] {
            Fixture f; f.ready(); f.receive("LED YELLOW ON\n");
            f.tick(2000); f.receive("LED YELLOW ON\n");
            f.tick(4000); lights(0,1,0); f.receive("LED GREEN");
            f.tick(4999); lights(0,1,0); f.tick(5000); lights(0,0,0);
        }},
        {"invalido apaga y no renueva timeout", [] {
            Fixture f; f.ready(); f.receive("LED RED BLINK\n");
            f.tick(1000); f.receive("ERROR FORMAT\n"); lights(0,0,0);
            f.tick(2999); check(fake::connected); f.tick(3000); check(!fake::connected);
        }},
        {"valido posterior a invalido puede activar", [] {
            Fixture f; f.ready(); f.receive("LED GREEN ON\n"); f.receive("FOO\n");
            lights(0,0,0); f.receive("LED YELLOW ON\n"); lights(0,1,0);
        }},
        {"linea de 128 bytes y desbordamiento inmediato", [] {
            Fixture f; f.ready(); f.receive("LED GREEN ON\n");
            f.receive(std::string(128,'A')+"\n"); lights(0,0,0); check(fake::connected);
            f.receive("LED GREEN ON\n"); f.receive(std::string(129,'A'));
            lights(0,0,0); check(!fake::connected);
        }},
        {"CRLF controles y no ASCII apagan", [] {
            for(const auto& bad : {std::string("LED GREEN ON\r\n"),std::string(1,'\0'),std::string(1,static_cast<char>(0xff))}) {
                fake::reset(); Fixture f; f.ready(); f.receive("LED GREEN ON\n"); f.receive(bad);
                lights(0,0,0); check(!fake::connected); check(fake::sessions.back()=="HELLO ACTUATOR\n");
            }
        }},
        {"TCP perdido y reconexion limpia a 2 s", [] {
            Fixture f; f.ready(); f.receive("LED GREEN ON\nLED RED");
            fake::connected=false; f.client.update(); lights(0,0,0);
            f.tick(1999); check(fake::tcpAttempts==1); f.tick(2000); check(fake::tcpAttempts==2);
            f.receive("OK\n"); lights(0,0,0); f.receive("LED YELLOW ON\n"); lights(0,1,0);
            check(fake::sessions.back()=="HELLO ACTUATOR\n");
        }},
        {"WiFi perdido apaga y reintenta a 2 s", [] {
            Fixture f; f.ready(); f.receive("LED GREEN ON\n");
            fake::wifi=0; f.client.update(); lights(0,0,0);
            f.tick(1999); check(fake::wifiAttempts==1); f.tick(2000); check(fake::wifiAttempts==2);
            fake::wifi=WL_CONNECTED; f.client.update(); f.receive("OK\n"); lights(0,0,0);
        }},
        {"fallo de conexion TCP reintenta", [] {
            fake::connectSucceeds=false; Fixture f; f.connect();
            f.tick(1999); check(fake::tcpAttempts==1); f.tick(2000); check(fake::tcpAttempts==2);
            lights(0,0,0);
        }},
        {"timeout de identificacion 5 s", [] {
            Fixture f; f.connect(); f.receive("O");
            f.tick(4999); check(fake::connected); f.tick(5000); check(!fake::connected); lights(0,0,0);
        }},
        {"identificacion rechazada no activa comandos posteriores", [] {
            Fixture f; f.connect(); f.receive("ERROR DUPLICATE\nLED GREEN ON\n");
            check(!fake::connected); lights(0,0,0);
        }},
        {"HELLO parcial se envia exactamente una vez", [] {
            fake::writeLimit=2; Fixture f; f.connect(); fake::receive("OK\n");
            for(int i=0;i<12;++i) f.client.update();
            f.receive("LED GREEN ON\n"); lights(0,0,1);
            check(fake::sessions.back()=="HELLO ACTUATOR\n");
        }},
        {"HELLO bloqueado se cancela a 200 ms", [] {
            fake::sendError=EAGAIN; Fixture f; f.connect();
            f.tick(199); check(fake::connected); f.tick(200); check(!fake::connected); lights(0,0,0);
        }},
        {"error de escritura deja LEDs apagados", [] {
            fake::sendError=EPIPE; Fixture f; f.connect(); check(!fake::connected); lights(0,0,0);
        }},
        {"rollover millis en parpadeo y watchdog", [] {
            fake::now=UINT32_MAX-999; Fixture f; f.ready(); f.receive("LED RED BLINK\n");
            fake::now+=500; f.client.update(); f.manager.update(); lights(0,0,0);
            fake::now+=500; f.client.update(); f.manager.update(); lights(1,0,0);
            fake::now+=2000; f.client.update(); f.manager.update(); lights(0,0,0); check(!fake::connected);
        }},
        {"configuracion vacia conserva estado seguro", [] {
            ActuatorManager m; m.begin(); ActuatorClient c(m,"","","0.0.0.0");
            c.begin(); c.update(); lights(0,0,0); check(fake::wifiAttempts==0);
        }},
        {"presupuesto de lectura evita monopolizar loop", [] {
            Fixture f; f.ready(); f.receive(std::string(300,'\n'));
            check(fake::incoming.size()==300-cfg::READ_BUDGET_BYTES); lights(0,0,0);
        }}
    };
    int failures=0;
    for(const auto& test:tests) {
        fake::reset();
        try {test.second(); std::cout<<"OK: "<<test.first<<'\n';}
        catch(const std::exception& e) {++failures; std::cerr<<"FAIL: "<<test.first<<": "<<e.what()<<'\n';}
    }
    std::cout<<tests.size()<<" pruebas, "<<failures<<" fallos\n";
    return failures ? 1 : 0;
}
#endif
