#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

#define STUDENT_ID             F("    F120840     ")
#define IMPLEMENTED_EXTENSIONS F("UDCHARS,FREERAM,EEPROM,RECENT,NAMES,SCROLL")

#define NCOLORS  7
#define BL_OFF 0x0
#define RED    0x1
#define GREEN  0x2
#define YELLOW 0x3
#define BLUE   0x4
#define PURPLE 0x5
#define TEAL   0x6
#define WHITE  0x7

#define SYNC_TIMEOUT   1000
#define SELECT_TIMEOUT 1000

#define TOP_LINE       0
#define BOTTOM_LINE    1
#define TOP_CURSOR     0, TOP_LINE
#define BOTTOM_CURSOR  0, BOTTOM_LINE

// column
#define ARROW_POSITION   0
#define ID_POSITION      1
#define DATA_POSITION    2
#define RECENT_POSITION  5
#define DESC_POSITION    10

#define displayTopChannel(ch)    displayChannel(TOP_LINE, ch)
#define displayBottomChannel(ch) displayChannel(BOTTOM_LINE, ch)

#define isCreateCommand(cmdId) (cmdId == 'C')
#define isValueCommand(cmdId)  (cmdId == 'V' || cmdId == 'X' || cmdId == 'N')
#define isOutOfRange(value)    (value < 0 || value > 255)

#define MAX_DESC_LENGTH  15
#define MAX_CMD_LENGTH   5


/* data types */
typedef unsigned int uint;
typedef unsigned long ulong;

typedef enum state_e { // TODO: finish
  INITIALISATION,
  SYNCHRONISATION,
  AFTER_SYNC,
  MAIN, // basically AWAITING_MESSAGE and AWAITING_PRESS
  READING_CREATE_COMMAND,

  UP_PRESSED,
  DOWN_PRESSED,
  SELECT_IS_HELD, // TODO: better name?
  SELECT_AWAITING_RELEASE,

  // HCI
  HCI_LEFT, // TODO
  HCI_RIGHT, // TODO

  TODO,
} State;

typedef enum scroll_state_e {
  SCROLL_START, // scrollIndex == 0
  SCROLLING,    // scrollIndex++
  SCROLL_END,   // scrollIndex = 0
} ScrollState;

/*
Benefits of implementing channels as a LL instead of array[26]:
- better memory usage as memory for a channel will be allocated on demand
- easier to get prev/next channel if some channels haven't been created

I could've used:
  Channel *array[26];
  for (size_t i = 0; i < 26; i++)
    array[i] = nullptr;
ahh I think that might be better
 - easier getting previous channel
 - logic is simpler cos not implementing linkedlist
*/
// singly-linked-list, impl. similar to a TreeSet<Byte> (Java)
// creating new channel will just insert it between 2 nodes
// takes 21 -> ~103 bytes
//* these don't need to be typedef'd because C++ ...
typedef struct channel_s {
  channel_s(char id) {
    this->id = id;
    this->setDescription("");
  }

  char id;
  String description;
  // EEPROM, use pointer to be able to check if channel's value has been set
  byte *data = nullptr;
  byte max = 255;
  byte min = 0;
  channel_s *next = nullptr;

  // SCROLL
  byte scrollIndex;
  ulong lastScrollTime;
  ScrollState scrollState;

  void setDescription(const String desc) {
    description = desc;
    // SCROLL, reset scrolling
    scrollIndex = lastScrollTime = 0;
    scrollState = SCROLL_START;
  }

  void setData(byte value) {
    if (data == nullptr)
      data = new byte;
    *data = value;
  }

  byte getData() {
    return data == nullptr ? 0 : *data;
  }

  static channel_s *headChannel;

  static channel_s *create(char id, const String description);
  static channel_s *channelForId(char id);
  static channel_s *channelBefore(const channel_s *ch);
  static channel_s *getBottom(const channel_s *topChannel);
  static bool canGoUp(const channel_s *topChannel);
  static bool canGoDown(const channel_s *topChannel);

private:
  static void insertChannel(channel_s *ch);
} Channel;

Channel *Channel::headChannel = nullptr;

void Channel::insertChannel(Channel *ch) {
  if (headChannel == nullptr || ch->id < headChannel->id) {
    ch->next = headChannel;
    headChannel = ch;
    return;
  }

  Channel *prev = headChannel;

  while (prev->next != nullptr) {
    if (ch->id < prev->next->id) {
      break;
    }
    prev = prev->next;
  }

  ch->next = prev->next;
  prev->next = ch;
}

// create new channel if not already created, else use new description
Channel *Channel::create(char id, const String description) {
  Channel *ch = Channel::channelForId(id);

  if (ch == nullptr) {
    Serial.print(F("DEBUG: Making new channel with id "));
    Serial.println(id);
    // shouldn't free/delete because 'all channels will be used'
    ch = new Channel(id);
    insertChannel(ch);
  }

  ch->setDescription(description);
  return ch;
}

Channel *Channel::channelForId(char id) {
  Channel *node = headChannel;

  while (node != nullptr) {
    if (node->id == id)
      return node;
    node = node->next;
  }

  return nullptr;
}

Channel *Channel::channelBefore(const Channel *ch) {
  if (ch == headChannel)
    return nullptr;

  Channel *node = headChannel;

  while (node->next != nullptr) {
    if (node->next == ch)
      return node;
    node = node->next;
  }

  return nullptr;
}

Channel *Channel::getBottom(const Channel *topChannel) {
  return topChannel == nullptr ? nullptr : topChannel->next;
}

bool Channel::canGoUp(const Channel *topChannel) {
  return topChannel != headChannel;
}

bool Channel::canGoDown(const Channel *topChannel) {
  return topChannel != nullptr && topChannel->next != nullptr && topChannel->next->next != nullptr;
}

/* function prototypes */
// main (is that gonna be state name?)
// reading commands (main)
void readCreateCommand(Channel **topChannel);
void readValueCommand(char cmdId);
void messageError(char cmdId, const String &cmd);
// handling button presses (main)
// TODO
// display
void displayChannel(uint8_t row, Channel *ch);
void clearChannelRow(uint8_t row);
void updateDisplay(Channel *topChannel);
void updateBacklight();
void selectDisplay();
// utils
String rightJustify3Digits(uint num);
void rightPad(String &str, size_t desiredLen);
void skipLine(Stream &s);

/* extensions */
// NAMES,SCROLL
void displayChannelName(int row, Channel *ch);


/* globals */
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
// Channel *headChannel = nullptr;
Channel *&headChannel = Channel::headChannel;


namespace UDCHARS {
  void displayUpArrow(bool display);
  void displayDownArrow(bool display);

  void displayUpArrow(bool display) {
    uint8_t ch = ' ';
    if (display) {
      byte upChevron[] = { B00100, B01010, B10001, B00100, B01010, B10001, B00000, B00000 };
      lcd.createChar(ch = 0, upChevron);
    }
    lcd.setCursor(ARROW_POSITION, TOP_LINE);
    lcd.write(ch);
  }

  void displayDownArrow(bool display) {
    uint8_t ch = ' ';
    if (display) {
      byte downChevron[] = { B00000, B10001, B01010, B00100, B10001, B01010, B00100, B00000 };
      lcd.createChar(ch = 1, downChevron);
    }
    lcd.setCursor(ARROW_POSITION, BOTTOM_LINE);
    lcd.write(ch);
  }
}

#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else // __ARM__
extern char *__brkval;
#endif // __arm__
namespace FREERAM {
  void displayFreeMemory(int row = BOTTOM_LINE);
  
  namespace { // private
    uint freeMemory() {
      char top;
      #ifdef __arm__
      return &top - reinterpret_cast<char*>(sbrk(0));
      #elif defined(CORE_TEENSY) || (ARDUINO > 103 && ARDUINO != 151)
      return &top - __brkval;
      #else // __arm__
      return __brkval ? &top - __brkval : &top - __malloc_heap_start;
      #endif // __arm__
    }
  }

  void displayFreeMemory(int row) {
    lcd.setCursor(0, row);
    lcd.print(F("Free bytes:"));
    lcd.print(freeMemory());
  }
}

namespace HCI {
  // TODO
}

/*
 * _ to avoid any name conflicts
 *
 * Each channel occupies 19 bytes in EEPROM:
 * 1 id
 * 1 max
 * 1 min
 * 1 desc_length
 * 15 desc

* I think I still might need some other error-checking mechanism
*/
namespace _EEPROM {
  #define addressForId(id)   ((id - 'A') * 19)
  #define maxOffset(idAddr)  (idAddr + 1)
  #define minOffset(idAddr)  (idAddr + 2)
  #define descOffset(idAddr) (idAddr + 3)

  void updateEEPROM(const Channel *ch);
  Channel *readEEPROM();

  namespace {
    void writeString(int offset, const String &desc) {
      const byte len = desc.length();
      EEPROM.update(offset, len);

      for (int i = 0; i < len; i++) {
        EEPROM.update(offset + 1 + i, desc[i]);
      }
    }

    // returns whether the length is valid or not
    bool readString(int offset, String &desc) {
      const byte len = EEPROM[ offset ];
      if (len > MAX_DESC_LENGTH)
        return false;

      for (int i = 0; i < len; i++) {
        desc += (char) EEPROM[ offset + 1 + i ];
      }

      return true;
    }

    Channel *readChannel(uint addr, char id) {
      String desc;
      // if the stored description isn't valid
      if (!readString(descOffset(addr), desc))
        return nullptr;

      byte max = EEPROM[ maxOffset(addr) ];

      byte min = EEPROM[ minOffset(addr) ];

      Channel *ch = new Channel(id);
      ch->setDescription(desc);
      ch->max = max;
      ch->min = min;

      return ch;
    }
  }

  void updateEEPROM(const Channel *ch) {
    if (ch == nullptr)
     return;

    uint addr = addressForId(ch->id);

    EEPROM.update(addr, ch->id);
    EEPROM.update(maxOffset(addr), ch->max);
    EEPROM.update(minOffset(addr), ch->min);

    writeString(descOffset(addr), ch->description);
  }

  // returns the head channel
  Channel *readEEPROM() {
    Channel *head = nullptr;
    Channel *tail = nullptr;

    for (char id = 'A'; id <= 'Z'; id++) {
      uint addr = addressForId(id);

      // basic verification
      byte readId = EEPROM.read(addr);
      if (readId != id)
        continue;

      Channel *ch = readChannel(addr, id);

      if (head == nullptr) {
        head = tail = ch;
      } else if (ch != nullptr) {
        tail->next = ch;
        tail = ch;
      }
    }

    return head;
  }

  // will cause reading that id to return nullptr
  void invalidate(char id) {
    EEPROM.update(addressForId(id), 0);
  }

  void invalidateAll() {
    for (char id = 'A'; id <= 'Z'; id++)
      EEPROM.update(addressForId(id), 0);
  }

}


// was difficult to think of how to impl RECENT!
// basic singly-linked-list with only tail addition (polling? is that the term)
// but with a max size, which once reached, adding will discard head value
namespace RECENT {
  #define MAX_SIZE 64

  void addRecentValue(byte val);

  void _displayAverage(int row, bool display);
  #define displayAverage(display) _displayAverage(TOP_LINE, display)

  void _displayMostRecentValue(int row, bool display);
  #define displayMostRecentValue(display) _displayMostRecentValue(BOTTOM_LINE, display)

  namespace {
    typedef struct node_s {
      node_s(byte val) {
        this->val = val;
        this->next = nullptr;
      }

      byte val;
      struct node_s *next;
    } RecentNode;

    uint calculateAverage();

    RecentNode *recentHead = nullptr;
    RecentNode *recentTail = nullptr; // keep track of recentTail for O(1) insertion instead of O(n)

    size_t _recentLen = 0;

    // could use a running sum to make this O(1)
    uint calculateAverage() { // O(n)
      if (recentHead == nullptr)
        return 0;

      uint sum = recentHead->val;
      RecentNode *node = recentHead->next;

      while (node != nullptr) {
        sum += node->val;
        node = node->next;
      }

      return round(sum / _recentLen);
    }

    // debug - to check if recentHead gets deleted
    void _addSixtyOnce() {
      static bool done = false;

      if (!done) {
        for (byte i = 0; i < 60; i++)
          RECENT::addRecentValue(random(0, 256));
        done = true;
      }
    }

    // debug
    void _printAll(Print &p) {
      p.print(F("DEBUG: r_len="));
      p.print(_recentLen);
      p.print(F(", ["));

      if (recentHead == nullptr) {
        p.println(F("DEBUG: ]"));
        return;
      }

      p.print(recentHead->val);
      RecentNode *node = recentHead->next;
      while (node != nullptr) {
        p.print(',');
        p.print(node->val);
        node = node->next;
      }

      p.println(F("DEBUG: ]"));
    }

  }

  void addRecentValue(byte val) {  // O(1)
    if (recentHead == nullptr) {
      recentHead = recentTail = new RecentNode(val);
      _recentLen = 1;
      return;
    }

    // because we only care about the most recent MAX_SIZE values, we can discard (delete)
    // the oldest value, kind of like an LRU cache
    if (_recentLen == MAX_SIZE) {
      RecentNode *oldHead = recentHead;
      recentHead = recentHead->next;
      delete oldHead;
      _recentLen--;
    }

    RecentNode *node = new RecentNode(val);
    recentTail->next = node;
    recentTail = node;
    _recentLen++;
  }

  void _displayMostRecentValue(int row, bool display) { // O(1)
    display &= recentTail != nullptr;

    lcd.setCursor(RECENT_POSITION, row);
    lcd.print(display ? ',' : ' ');

    String tailVal = display ? rightJustify3Digits(recentTail->val) : F("   ");
    lcd.print(tailVal);
  }

  void _displayAverage(int row, bool display) {
    display &= recentHead != nullptr;

    lcd.setCursor(RECENT_POSITION, row);
    lcd.print(display ? ',' : ' ');

    String avrg = display ? rightJustify3Digits(calculateAverage()) : F("   ");
    lcd.print(avrg);
  }
}

namespace NAMES_SCROLL {

}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.clear();
}

void loop() {
  static State state = INITIALISATION;
  static Channel *topChannel; // btmChannel = topChannel->next
  static ulong selectPressTime;

  uint8_t b;

  switch (state) {
  case INITIALISATION:
    selectPressTime = 0;

    // EEPROM
    topChannel = headChannel = _EEPROM::readEEPROM();
    _printChannelsFull(Serial, topChannel);

    state = SYNCHRONISATION;
    break;

  case SYNCHRONISATION:
    static ulong lastQTime = 0;

    lcd.setBacklight(PURPLE);

    if (millis() - lastQTime >= SYNC_TIMEOUT) {
      Serial.print('Q');
      lastQTime = millis();
    }
    if (Serial.available() && Serial.read() == 'X') {
      state = AFTER_SYNC;
    }
    break;

  case AFTER_SYNC:
    Serial.println(IMPLEMENTED_EXTENSIONS);
    state = MAIN;
    break;

  case MAIN: // basically AWAITING_PRESS & AWAITING_MESSAGE
    updateDisplay(topChannel);
    b = lcd.readButtons();

    if (b & BUTTON_SELECT) {
      Serial.println(F("DEBUG: Select pressed"));
      selectPressTime = millis();
      state = SELECT_IS_HELD;
      break;
    } else if (b & BUTTON_UP) {
      Serial.println(F("DEBUG: Up pressed"));
      state = UP_PRESSED;
      break;
    } else if (b & BUTTON_DOWN) {
      Serial.println(F("DEBUG: Down pressed"));
      state = DOWN_PRESSED;
      break;
    } // TODO: button_left & button_right

    if (Serial.available()) {
      char cmdId = Serial.read();

      if (isCreateCommand(cmdId))
        readCreateCommand(&topChannel);
      else if (isValueCommand(cmdId))
        readValueCommand(cmdId);
      else
        skipLine(Serial);
    }

    break;

  // may not need to be a state? cos really its not (but left/right are)
  //? make into a func?
  case UP_PRESSED:
    if (Channel::canGoUp(topChannel))
      topChannel = Channel::channelBefore(topChannel);
    state = MAIN;
    break;

  // may not need to be a state? cos really its not
  //? make into a func?
  case DOWN_PRESSED:
    if (Channel::canGoDown(topChannel))
      topChannel = topChannel->next;
    state = MAIN;
    break;

  //! TODO: refactor so handle Serial input
  case SELECT_IS_HELD:  // select is currently being held, waiting to reach 1 second
    // if select has been held for 1 second
    if (millis() - selectPressTime >= SELECT_TIMEOUT) {
      Serial.println(F("DEBUG: SELECT has been held for 1s"));
      lcd.clear();
      selectDisplay();
      state = SELECT_AWAITING_RELEASE;
    } else {
      // if select has been released
      if (!(lcd.readButtons() & BUTTON_SELECT)) {
        Serial.println(F("DEBUG: SELECT released before 1s"));
        //? might need like sub-state is main own FSM...
        state = MAIN;
      } else {
        Serial.println(F("DEBUG: Timeout until SELECT held for 1s"));
      }
    }
    break;

  //! TODO: refactor so handle Serial input
  //* maybe make a SelectState
  //* or DisplayState - I think this makes more sense (NORMAL, SELECT)
  case SELECT_AWAITING_RELEASE:  // select is currently being held (has already been held for 1+ second
    Serial.println(F("DEBUG: Awaiting SELECT release"));
    // if SELECT has been released
    if (!(lcd.readButtons() & BUTTON_SELECT)) {
      Serial.println(F("DEBUG: SELECT released"));
      lcd.clear();
      //? might need to store like a sub-state if main is its own FSM?
      state = MAIN;
    }
    break;

  case TODO:
    /*
    static int i = 0;
    static char id = 'A';
    static bool top = true;

    Channel &ch = channels[i];
    ch.data = random(256);
    displayChannel(top ? 0 : 1, ch);
    // lcd.setBacklight(i % 7);
    lcd.setBacklight(random(8));
    delay(800);

    ++i %= 26;
    top = !top;
    */
    lcd.setBacklight(YELLOW);
    break;
  }
}

// debug
void _printChannels(Print &p) {
  // p.print(F("DEBUG: ch_len?"));
  // p.print(_len);
  p.print(F("DEBUG: channels=["));
  // p.print(F(" ["));

  if (headChannel == nullptr) {
    p.println(']');
    return;
  }

  p.print(headChannel->id);
  //_printChannel(Serial, headChannel)
  Channel *node = headChannel->next;
  while (node != nullptr) {
    p.print(',');
    p.print(node->id);
    node = node->next;
  }

  p.println(']');
}

// debug
void _printChannel(Print &p, Channel *ch, bool newLine = true);
void _printChannel(Print &p, Channel *ch, bool newLine) {
  p.print(F("DEBUG: Channel "));
  p.print(ch->id);

  p.print(F(", data="));
  p.print(ch->getData());

  p.print(F(", max="));
  p.print(ch->max);

  p.print(F(", min="));
  p.print(ch->min);

  p.print(F(", description={"));
  p.print(ch->description);
  p.print('}');

  if (newLine)
    p.println();
}

void _printChannelsFull(Print &p) {
  p.println(F("DEBUG: fullChannels = ["));

  if (headChannel == nullptr) {
    p.println(F("DEBUG: ]"));
    return;
  }

  _printChannel(Serial, headChannel);
  Channel *node = headChannel->next;
  while (node != nullptr) {
    _printChannel(Serial, node);
    node = node->next;
  }

  p.println(F("DEBUG: ]"));
}

void _printChannelsFull(Print &p, Channel* head) {
  p.println(F("DEBUG: fullChannels = ["));

  if (head == nullptr) {
    p.println(F("DEBUG: ]"));
    return;
  }

  _printChannel(Serial, head);
  Channel *node = head->next;
  while (node != nullptr) {
    _printChannel(Serial, node);
    node = node->next;
  }

  p.println(F("DEBUG: ]"));
}


/* reading commands */

void readCreateCommand(Channel **topChannel) {
  String cmd = Serial.readStringUntil('\n');

  uint cmdLen = cmd.length();
  if (cmdLen < 2 || !isUpperCase(cmd[ 0 ])) {
    messageError('C', cmd);
    return;
  }

  char channelId = cmd[ 0 ];
  String description = cmd.substring(1, min(cmdLen, 1 + MAX_DESC_LENGTH));

  Channel *ch = Channel::create(channelId, description);

  // if creating first channel
  if (*topChannel == nullptr) {
    Serial.println(F("DEBUG: FIRST CHANNEL MADE"));
    *topChannel = ch;
  }

  _EEPROM::updateEEPROM(ch);
}

void readValueCommand(char cmdId) {
  String cmd = Serial.readStringUntil('\n');

  uint cmdLen = cmd.length();
  if (cmdLen < 2 || cmdLen > 4) {
    messageError(cmdId, cmd);
    return;
  }

  char channelId = cmd[ 0 ];
  String valueS = cmd.substring(1);
  long value = valueS.toInt();

  if (!isUpperCase(channelId)
      || (value == 0 && valueS != "0") // input wasn't numeric
      || isOutOfRange(value)
     ) {
    messageError(cmdId, cmd);
    return;
  }

  Channel *ch = Channel::channelForId(channelId);
  // if channel hasn't been created, don't do anything
  if (ch == nullptr)
    return;

  if (cmdId == 'V') {
    RECENT::addRecentValue(value);
    ch->setData(value);
  } else if (cmdId == 'X')
    ch->max = value;
  else
    ch->min = value;

  _EEPROM::updateEEPROM(ch);
}

void messageError(char cmdId, const String &cmd) {
  Serial.print(F("ERROR: "));
  Serial.print(cmdId);
  Serial.println(cmd);
}

/* display */

void displayChannel(uint8_t row, Channel *ch) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(ch->id);
  lcd.setCursor(DATA_POSITION, row);
  lcd.print(rightJustify3Digits(ch->getData()));

  // NAMES
  displayChannelName(row, ch);
}

void clearChannelRow(uint8_t row) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(F("    "));
}

void updateDisplay(Channel *const topChannel) {
  updateBacklight();

  // UDCHARS,HCI
  UDCHARS::displayUpArrow(Channel::canGoUp(topChannel));
  UDCHARS::displayDownArrow(Channel::canGoDown(topChannel));

  if (topChannel != nullptr) {
    displayTopChannel(topChannel);
  } else {
    clearChannelRow(TOP_LINE);
  }

  Channel *const btmChannel = Channel::getBottom(topChannel);
  if (btmChannel != nullptr) {
    displayBottomChannel(btmChannel);
  } else {
    clearChannelRow(BOTTOM_LINE);
  }

  // RECENT
  RECENT::displayAverage(/*TOP_LINE, */topChannel != nullptr);
  RECENT::displayMostRecentValue(/*BOTTOM_LINE, */btmChannel != nullptr);
}

/*
- All values in every channel in range: white
- Any number above max: red
- Any number below min: green
- If both: yellow
*/
void updateBacklight() {
  // take advantage of the fact that YELLOW == RED | GREEN
  uint color = 0;

  Channel *ch = headChannel;

  while (ch != nullptr) {
    // ignore channel if its value hasn't been set by a command
    if (ch->data == nullptr) {
      ch = ch->next;
      continue;
    }

    if (ch->getData() > ch->max)
      color |= RED;
    else if (ch->getData() < ch->min)
      color |= GREEN;

    // early exit, already reached worst case
    if (color == YELLOW)
      break;

    ch = ch->next;
  }

  color = color == 0 ? WHITE : color;
  lcd.setBacklight(color);
}

void selectDisplay() {
  lcd.setBacklight(PURPLE);

  lcd.setCursor(TOP_CURSOR);
  lcd.print(STUDENT_ID);

  // FREERAM
  FREERAM::displayFreeMemory();
}

/* Utility functions */

String rightJustify3Digits(uint num) {
  if (num >= 100)
    return String(num);

  String prefix = (num >= 10) ? F(" ") : F("  ");
  prefix.concat(num);
  return prefix;
}

// pad spaces to the right of given string, to help overwrite old values
void rightPad(String &str, size_t desiredLen) {
  int diff = desiredLen - str.length();
  while (diff > 0) {
    str.concat(' ');
    diff--;
  }
}

//! may not use
void skipLine(Stream &s) {
  s.flush();
  s.find('\n');
}
