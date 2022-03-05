#include <EEPROM.h>
#include <avr/eeprom.h>

/*
A channel will take up 19 bytes in EEPROM:
  1 id
  1 max
  1 min
  1 desc_length
  15 desc

* I think I still might need some other error-checking mechanism

*/

#define addressForId(id) ((id - 'A') * 19)

#define maxOffset(idAddr)  (idAddr + 1)
#define minOffset(idAddr)  (idAddr + 2)
#define descOffset(idAddr) (idAddr + 3)

void updateEEPROM(const Channel *ch);
Channel *readEeprom();

void updateEEPROM(const Channel *ch) {
  if (ch == nullptr)
    return;

  uint addr = addressForId(ch->id);

  EEPROM.update(addr, ch->id);
  EEPROM.update(maxOffset(addr), ch->max);
  EEPROM.update(minOffset(addr), ch->min);

  eepromWriteString(descOffset(addr), ch->description);
}

void eepromWriteString(int offset, const String &desc) {
  const byte len = desc.length();
  EEPROM.update(offset, len);

  for (int i = 0; i < len; i++) {
    EEPROM.update(offset + 1 + i, desc[i]);
  }
}

// returns the head channel
Channel *readEeprom() {
  Channel *head = nullptr;
  Channel *tail = nullptr;

  for (char id = 'A'; id <= 'Z'; id++) {
    uint addr = addressForId(id);

    // basic verification
    byte readId = EEPROM.read(addr);
    if (readId != id)
      continue;

    Channel *ch = readChannelFromEeprom(addr, id);

    if (head == nullptr) {
      head = tail = ch;
    } else {
      tail->next = ch;
      tail = ch;
    }
  }

  return head;
}

Channel *readChannelFromEeprom(uint addr, char id) {
  String desc;
  // if the stored description isn't valid
  if (!eepromReadString(descOffset(addr), desc))
    return nullptr;

  byte max = EEPROM[ maxOffset(addr) ];

  byte min = EEPROM[ minOffset(addr) ];

  Channel *ch = new Channel(id);
  ch->setDescription(desc);
  ch->max = max;
  ch->min = min;

  return ch;
}

// returns whether the length is valid or not
bool eepromReadString(int offset, String &desc) {
  const byte len = EEPROM[ offset ];
  if (len > MAX_DESC_LENGTH)
    return false;

  for (int i = 0; i < len; i++) {
    desc += (char) EEPROM[ offset + 1 + i ];
  }

  return true;
}

// will cause reading that id to return nullptr
void invalidate(char id) {
  EEPROM.update(addressForId(id), 0);
}
void invalidateAll() {
  for (char id = 'A'; id <= 'Z'; id++) {
    EEPROM.update(addressForId(id), 0);
  }
}
