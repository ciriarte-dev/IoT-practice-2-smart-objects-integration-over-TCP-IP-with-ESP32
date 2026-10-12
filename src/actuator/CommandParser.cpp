#include "CommandParser.h"

#include <string.h>

namespace {
struct Entry {
  const char* text;
  Command cmd;
};

// Única tabla de comandos: agregar uno nuevo = agregar una fila.
const Entry kCommands[] = {
    {"LED RED BLINK", Command::RED_BLINK},
    {"LED YELLOW ON", Command::YELLOW_ON},
    {"LED GREEN ON", Command::GREEN_ON},
    {"LED ALL OFF", Command::ALL_OFF},
};
}  // namespace

Command CommandParser::parse(const char* text) {
  if (text == nullptr) {
    return Command::INVALID;
  }

  for (const Entry& e : kCommands) {
    if (strcmp(text, e.text) == 0) {
      return e.cmd;
    }
  }
  return Command::INVALID;
}

const char* CommandParser::toText(Command cmd) {
  for (const Entry& e : kCommands) {
    if (e.cmd == cmd) {
      return e.text;
    }
  }
  return "NONE";
}
