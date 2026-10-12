#include "CommandParser.h"

#include <ctype.h>
#include <string.h>
#include <strings.h>

namespace {
struct Entry {
  const char* text;
  Command cmd;
};

// Única tabla de comandos: agregar uno nuevo = agregar una fila.
const Entry kCommands[] = {
    {"RED_BLINK", Command::RED_BLINK},
    {"YELLOW_ON", Command::YELLOW_ON},
    {"GREEN_ON", Command::GREEN_ON},
};
} 

Command CommandParser::parse(const char* text) {
  if (text == nullptr) {
    return Command::INVALID;
  }

  // Recortar espacios al inicio y al final.
  while (isspace(static_cast<unsigned char>(*text))) {
    ++text;
  }
  size_t len = strlen(text);
  while (len > 0 && isspace(static_cast<unsigned char>(text[len - 1]))) {
    --len;
  }

  for (const Entry& e : kCommands) {
    if (strlen(e.text) == len && strncasecmp(text, e.text, len) == 0) {
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
