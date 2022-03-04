#include <EEPROM.h>
#include <avr/eeprom.h> // TODO: EEPROM

/*
try and do some sort of operation on id so
that there's a distance between them of at least (bytes):
 15 (desc)
 +1  (max)
 +1  (min)

A channel will take up 18 bytes:
  1 id
  15 desc
  1 max
  1 min

*/

// use eeprom_crc() to check if updated?

#define descLocation(idAddress)  idAddress + 1
#define maxLocation(idAddress)   descLocation(idAddress) + MAX_DESC_LENGTH + 1
#define minLocation(idAddress)   maxLocation(idAddress) + 1

void readEeprom();
void updateEEPROMDesc(char id, const String desc);
void updateEepromMax(char id, byte max);
void updateEepromMin(char id, byte min);

void readEeprom() {
  for (char id = 'A'; id <= 'Z'; id++) {
    int addr = _address(id);
    int readId = EEPROM.read(addr);
    if (readId != id)
      continue;

    String desc;
    EEPROM.get(descLocation(addr), desc);
    byte max;
    EEPROM.get(maxLocation(addr), max);
    byte min;
    EEPROM.get(minLocation(addr), min);

    Channel *ch = new Channel(id);
    ch->setDescription(desc);
    ch->max = max;
    ch->min = min;
  }
}

void updateEEPROMDesc(char id, const String desc) {
  int addr = _writeId(id);
  EEPROM.put(descLocation(addr), desc);
}

void updateEepromMax(char id, byte max) {
  int addr = _writeId(id);
  EEPROM.put(maxLocation(addr), max);
}

void updateEepromMin(char id, byte min) {
  int addr = _writeId(id);
  EEPROM.put(minLocation(addr), min);
}

int _address(char id) {
  return (id - 'A') * 18;
}

int _writeId(char id) {
  int addr = _address(id);
  EEPROM.update(addr, id);
  return addr;
}
