#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

#define STUDENT_ID             F("    F120840     ")
#define IMPLEMENTED_EXTENSIONS F("UDCHARS,FREERAM,EEPROM,RECENT,NAMES,SCROLL")

// change defines to const int?

#define NCOLORS  7
#define BL_OFF 0x0
#define RED    0x1
#define GREEN  0x2
#define YELLOW 0x3
#define BLUE   0x4
#define PURPLE 0x5
#define TEAL   0x6
#define WHITE  0x7

#define UP_ARROW_CHAR   0
#define DOWN_ARROW_CHAR 1

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

#define isCreateCommand(cmdId) ((cmdId) == 'C')
#define isValueCommand(cmdId)  ((cmdId) == 'V' || (cmdId) == 'X' || (cmdId) == 'N')
#define isOutOfRange(value)    ((value) < 0 || (value) > 255)

#define MAX_DESC_LENGTH  15
#define MAX_CMD_LENGTH   5

// RECENT
#define MAX_RECENT_SIZE 64


// FREERAM, lab sheet 5
#ifdef __arm__
// should use uinstd.h to define sbrk but Due causes a conflict
extern "C" char* sbrk(int incr);
#else // __ARM__
extern char *__brkval;
#endif // __arm__


/* data types */
typedef unsigned int uint;
typedef unsigned long ulong;

typedef enum { // TODO: finish
  INITIALISATION,
  SYNCHRONISATION,
  AFTER_SYNC,
  MAIN, // basically AWAITING_MESSAGE and AWAITING_PRESS

  UP_PRESSED,
  DOWN_PRESSED,
  SELECT_IS_HELD, // TODO: better name?
  SELECT_AWAITING_RELEASE,

  // HCI
  HCI_LEFT, // TODO
  HCI_RIGHT, // TODO
} State;

// affect get_bottom
typedef enum {
  NORMAL,
  LEFT,
  RIGHT,
} HciState;

typedef enum {
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
typedef struct channel_s {
  channel_s(char id, const char *desc, byte descLen) {
    this->id = id;
    this->setDescription(desc, descLen);
  }

  char id;
  const char *desc = nullptr;
  byte descLen;

  // channel data/value can be gotten using recentTail->val
  byte max = 255;
  byte min = 0;
  channel_s *next = nullptr;

  // SCROLL
  byte scrollIndex;
  ulong lastScrollTime;
  ScrollState scrollState;

  void setDescription(const char *desc, byte descLen) {
    if (this->desc != nullptr)
      free((char*) this->desc);
    this->desc = desc;
    this->descLen = descLen;
    // SCROLL, reset scrolling
    scrollIndex = lastScrollTime = 0;
    scrollState = SCROLL_START;
  }

  bool valueHasBeenSet() const {
    return recentTail != nullptr;
  }

  // RECENT
  /*
  was difficult to think of how to impl RECENT!
  basic singly-linked-list with only tail addition (polling? is that the term)
  but with a max size, which once reached, adding will discard head value
  it's a Queue! FIFO
  */
private:
  typedef struct node_s {
    node_s(byte val) {
      this->val = val;
      this->next = nullptr;
    }

    byte val;
    struct node_s *next;
  } RecentNode;

  RecentNode *recentHead = nullptr;
  RecentNode *recentTail = nullptr;
  byte recentLen = 0; // max MAX_RECENT_SIZE

  // RECENT
  void addRecent(byte val) {
    if (recentHead == nullptr) {
      recentHead = recentTail = new RecentNode(val);
      recentLen = 1;
      return;
    }

    // because we only care about the most recent MAX_RECENT_SIZE values, we can discard (delete)
    // the oldest value, kind of like an LRU cache
    if (recentLen == MAX_RECENT_SIZE) {
      RecentNode *oldHead = recentHead;
      recentHead = recentHead->next;
      delete oldHead;
      recentLen--;
    }

    RecentNode *node = new RecentNode(val);
    recentTail->next = node;
    recentTail = node;
    recentLen++;
  }

  // debug - to help check if recentHead gets deleted
  bool _addedSixty = false;
  void _addSixtyRecentsOnce() {
    if (!_addedSixty) {
      for (uint i = 0; i < 60; i++)
        addRecent(random(0, 256));
      _addedSixty = true;
    }
  }

public:
  // RECENT
  byte getAverageValue() const {
    if (recentHead == nullptr)
      return 0;

    uint sum = recentHead->val;
    RecentNode *node = recentHead->next;

    while (node != nullptr) {
      sum += node->val;
      node = node->next;
    }

    return round(sum / recentLen);
  }

  // debug
  void _printAllRecent(Print &p) const {
    p.print(F("DEBUG: "));
    p.print(id);
    p.print(F(" r_len="));
    p.print(recentLen);
    p.print(F(", ["));

    if (recentHead == nullptr) {
      p.println(']');
      return;
    }

    p.print(recentHead->val);
    RecentNode *node = recentHead->next;
    while (node != nullptr) {
      p.print(',');
      p.print(node->val);
      node = node->next;
    }

    p.println(']');
  }

  void setData(byte value) {
    addRecent(value);
    _printAllRecent(Serial);
  }

  byte getData() const {
    return recentTail == nullptr ? 0 : recentTail->val;
  }

  static channel_s *headChannel;

  // these methods assume provided ID is valid (A-Z)
  //static channel_s *create(char id, String description);
  static channel_s *create(char id, const char *desc, byte descLen);
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
Channel *Channel::create(char id, const char *desc, byte descLen) {
  Channel *ch = Channel::channelForId(id);

  if (ch == nullptr) {
    Serial.print(F("DEBUG: Making new channel with id "));
    Serial.println(id);
    // shouldn't free/delete because 'all channels will be used'
    ch = new Channel(id, desc, descLen);
    insertChannel(ch);
  } else {
    ch->setDescription(desc, descLen);
  }

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
void handleSerialInput(Channel **topChannelPtr);
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
void skipLine(Stream &s);


/* globals */
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
Channel *&headChannel = Channel::headChannel;


/* extensions */

/*
 * Arrows are 2 chevrons
 */
namespace UDCHARS {
  void displayUpArrow(bool display);
  void displayDownArrow(bool display);

  void displayUpArrow(bool display) {
    uint8_t ch = display ? UP_ARROW_CHAR : ' ';
    lcd.setCursor(ARROW_POSITION, TOP_LINE);
    lcd.write(ch);
  }

  void displayDownArrow(bool display) {
    uint8_t ch = display ? DOWN_ARROW_CHAR : ' ';
    lcd.setCursor(ARROW_POSITION, BOTTOM_LINE);
    lcd.write(ch);
  }
}

/*
 * Displayed on the bottom line of the select display
 */
namespace FREERAM {
  void displayFreeMemory(int row = BOTTOM_LINE);

  namespace { // 'private', helper methods
    // week 3 lab
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
    lcd.setCursor(1, row);
    lcd.print(F("Free bytes:"));
    lcd.print(freeMemory());
  }
}

/*
? maybe change topChannel to channel ID (char)
*/
namespace HCI {
  // TODO
}

/*
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
    void writeString(int offset, const char *desc, byte len) {
      EEPROM.update(offset, len);
      eeprom_update_block(desc, (void*) (offset + 1), len);
    }

    void readString(int offset, char *desc, byte len) {
      eeprom_read_block(desc, (void*) offset, len);
      desc[ len ] = '\0';
    }

    Channel *readChannel(char id) {
      const uint idAddr = addressForId(id);

      // basic verification
      if (EEPROM[ idAddr ] != id)
        return nullptr;

      byte descLen =  EEPROM[ descOffset(idAddr) ];
      // if the stored description isn't valid
      if (descLen == 0 || descLen > MAX_DESC_LENGTH)
        return nullptr;

      char *desc = (char*) malloc((1 + descLen) * sizeof(*desc));
      readString(descOffset(idAddr) + 1, desc, descLen);

      byte max = EEPROM[ maxOffset(idAddr) ];

      byte min = EEPROM[ minOffset(idAddr) ];

      Channel *ch = new Channel(id, desc, descLen);
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

    writeString(descOffset(addr), ch->desc, ch->descLen);
  }

  // returns the head channel
  Channel *readEEPROM() {
    Channel *head = nullptr;
    Channel *tail = nullptr;

    for (char id = 'A'; id <= 'Z'; id++) {
      Channel *ch = readChannel(id);

      if (head == nullptr) {
        head = tail = ch;
      } else if (ch != nullptr) {
        tail->next = ch;
        tail = ch;
      }
    }

    return head;
  }

  // debug - will cause reading that id to return nullptr
  void invalidate(char id) {
    EEPROM.update(addressForId(id), 0);
  }

  // debug
  void invalidateAll() {
    for (char id = 'A'; id <= 'Z'; id++)
      EEPROM.update(addressForId(id), 0);
  }

}

// NAMES and SCROLL together, it makes sense
/*
 * [currently] Uses a FSM to manage scrolling state, uses Channel variables
 * scrollState, scrollIndex & lastScrollTime.
 */
namespace NAMES_SCROLL {
  #define SCROLL_CHARS      1 //2
  #define SCROLL_TIMEOUT    500 //1000
  #define DESC_DISPLAY_LEN  6

  void displayChannelName(int row, Channel *ch);

  void displayChannelName(int row, Channel *ch) {
    // ch->desc SHOULD NOT be nullptr

    const byte dLen = ch->descLen;
    byte &si = ch->scrollIndex;

    // NAMES
    lcd.setCursor(DESC_POSITION, row);
    for (int i = si; i < si + DESC_DISPLAY_LEN; i++) {
      // display space(s) at end to overwrite old displayed value
      char c = (i < dLen) ? ch->desc[ i ] : ' ';
      lcd.print(c);
    }

    ScrollState &state = ch->scrollState;

    // SCROLL
    /*
    tbh this could just be a bunch of ifs, it's not really a state
    its more like a flowchart ish
    */
    switch (state) {
      // not really a state
      case SCROLL_START:
        // only need to scroll if channel desc is too big
        if (dLen > DESC_DISPLAY_LEN)
          state = SCROLLING;
        break;

      case SCROLLING:
        if (millis() - ch->lastScrollTime >= SCROLL_TIMEOUT) {
          ch->scrollIndex += SCROLL_CHARS;
          // once full desc has been displayed, return to start
          if (ch->scrollIndex + DESC_DISPLAY_LEN > dLen + 1) { // +1 to make even lengths work (because of 'trailing' char)
            ch->scrollIndex = 0;
            state = SCROLL_END;
          }
          ch->lastScrollTime = millis();
        }
        break;

      // not really needed? can be handled in SCROLLING
      case SCROLL_END:
        ch->scrollIndex = 0;
        state = SCROLL_START;
        break;
    }
  }
}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.clear();

  // UDCHARS
  byte upChevron[] = { B00100, B01010, B10001, B00100, B01010, B10001, B00000, B00000 };
  lcd.createChar(UP_ARROW_CHAR, upChevron);
  byte downChevron[] = { B00000, B10001, B01010, B00100, B10001, B01010, B00100, B00000 };
  lcd.createChar(DOWN_ARROW_CHAR, downChevron);
}

void loop() {
  static State state = INITIALISATION;
  static Channel *topChannel; // btmChannel ~= topChannel->next
  static ulong selectPressTime;

  uint8_t b;

  switch (state) {
  case INITIALISATION:
    topChannel = headChannel = nullptr;
    selectPressTime = 0;
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

    // EEPROM
    topChannel = headChannel = _EEPROM::readEEPROM();
    Serial.println(F("DEBUG: Channels stored in EEPROM:"));
    _printChannelsFull(Serial, topChannel);

    state = MAIN;
    break;

  case MAIN: // basically AWAITING_PRESS & AWAITING_MESSAGE
    b = lcd.readButtons();

    if (b & BUTTON_SELECT) {
      Serial.println(F("DEBUG: Select pressed"));
      selectPressTime = millis();
      state = SELECT_IS_HELD;
      break;
    } else if (b & BUTTON_UP) {
      Serial.println(F("DEBUG: Up pressed"));
      if (Channel::canGoUp(topChannel))
        topChannel = Channel::channelBefore(topChannel);
      state = UP_PRESSED;
      break;
    } else if (b & BUTTON_DOWN) {
      Serial.println(F("DEBUG: Down pressed"));
      if (Channel::canGoDown(topChannel))
        topChannel = topChannel->next;
      state = DOWN_PRESSED;
      break;
    } else if (b & BUTTON_LEFT) {

    } else if (b & BUTTON_RIGHT) {

    }
    // TODO: button_left & button_right

    handleSerialInput(&topChannel);

    updateDisplay(topChannel);
    break;

  // wait until up is released
  case UP_PRESSED:
    if (!(lcd.readButtons() & BUTTON_UP))
      state = MAIN;
    break;

  // wait until down is released
  case DOWN_PRESSED:
    if (!(lcd.readButtons() & BUTTON_DOWN))
      state = MAIN;
    break;

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

  //* maybe make a SelectState
  //* or DisplayState - I think this makes more sense (NORMAL, SELECT)
  case SELECT_AWAITING_RELEASE:  // select is currently being held (has already been held for 1+ second)
    Serial.println(F("DEBUG: Awaiting SELECT release"));
    // if SELECT has been released
    if (!(lcd.readButtons() & BUTTON_SELECT)) {
      Serial.println(F("DEBUG: SELECT released"));
      lcd.clear();
      //? might need to store like a sub-state if main is its own FSM?
      state = MAIN;
    }

    handleSerialInput(&topChannel);
    break;
  }
}

void handleSerialInput(Channel **topChannelPtr) {
  if (!Serial.available())
    return;

  char cmdId = Serial.read();

  if (isCreateCommand(cmdId))
    readCreateCommand(topChannelPtr);
  else if (isValueCommand(cmdId))
    readValueCommand(cmdId);
  else // TODO: error
    skipLine(Serial);
}

/* reading commands */

void readCreateCommand(Channel **topChannelPtr) {
  if (Serial.available() < 2)
    return messageError('C', Serial.readStringUntil('\n'));

  char channelId = Serial.read();

  char *desc = (char*) malloc((1 + MAX_DESC_LENGTH) * sizeof(*desc));
  byte descLen = Serial.readBytesUntil('\n', desc, MAX_DESC_LENGTH);
  desc[ descLen ] = '\0';

  if (!isUpperCase(channelId)) {
    messageError('C', channelId, desc);
    free(desc);
    return;
  }
  if (descLen < MAX_DESC_LENGTH) {
    // reallocate a shorter buffer
    desc = (char*) realloc(desc, (1 + descLen) * sizeof(*desc));
  } else if (descLen == MAX_DESC_LENGTH) {
    // ignore chars after 15th description character
    skipLine(Serial);
  }

  Serial.print(F("DEBUG: DESC: ["));
  Serial.print(desc);
  Serial.println(']');

  Channel *ch = Channel::create(channelId, desc, descLen);

  // if creating first channel
  if (*topChannelPtr == nullptr) {
    Serial.println(F("DEBUG: FIRST CHANNEL MADE"));
    *topChannelPtr = ch;
  }

  _EEPROM::updateEEPROM(ch);
}

void readValueCommand(char cmdId) {
  String cmd = Serial.readStringUntil('\n');

  uint cmdLen = cmd.length();
  if (cmdLen < 2 || cmdLen > 4)
    return messageError(cmdId, cmd);

  char channelId = cmd[ 0 ];
  String valueS = cmd.substring(1);
  long value = valueS.toInt();

  if (!isUpperCase(channelId)
      || (value == 0 && valueS != "0") // input wasn't numeric
      || isOutOfRange(value)
     )
     return messageError(cmdId, cmd);

  Channel *ch = Channel::channelForId(channelId);
  // if channel hasn't been created, don't do anything
  if (ch == nullptr)
    return;

  switch (cmdId) {
    case 'V':
      ch->setData(value);
      break;
    case 'X':
      ch->max = value;
      break;
    case 'N':
      ch->min = value;
      break;
  }

  _EEPROM::updateEEPROM(ch);
}

void messageError(char cmdId, const String &cmd) {
  Serial.print(F("ERROR: "));
  Serial.print(cmdId);
  Serial.println(cmd);
}

void messageError(char cmdId, char channelId, const char *rest) {
  Serial.print(F("ERROR: "));
  Serial.print(cmdId);
  Serial.print(channelId);
  Serial.println(rest);
}

/* display */

void displayChannel(uint8_t row, Channel *ch) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(ch->id);
  lcd.setCursor(DATA_POSITION, row);
  lcd.print(rightJustify3Digits(ch->getData()));

  // RECENT
  lcd.setCursor(RECENT_POSITION, row);
  if (ch->valueHasBeenSet()) {
    lcd.print(',');
    lcd.print(rightJustify3Digits(ch->getAverageValue()));
  } else
    lcd.print(F("    "));

  // NAMES,SCROLL
  NAMES_SCROLL::displayChannelName(row, ch);
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
    if (!ch->valueHasBeenSet()) {
      ch = ch->next;
      continue;
    }

    if (ch->getData() > ch->max)
      color |= RED;
    else if (ch->min <= ch->max && ch->getData() < ch->min)
      color |= GREEN;

    // early exit, already reached 'worst case'
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

void skipLine(Stream &s) {
  s.find('\n');
}

// debug
void _printChannelIds(Print &p) {
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
  p.print(ch->desc);
  p.print('}');
  p.print('@');
  p.print((size_t) ch->desc);

  p.print(F(", descLen="));
  p.print(ch->descLen);

  if (newLine)
    p.println();
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
