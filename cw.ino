#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
#include <EEPROM.h>
#include <avr/eeprom.h>

// RECENT: RECENT_MODE
#define LL 0    // linked-list
#define ARRAY 1 // circular array
#define EMA 2   // exponential moving average

// implementation flags
#define DEBUG true
#define RECENT_MODE ARRAY // RECENT: LL/ARRAY/EMA

#ifndef DEBUG
  #define DEBUG false
#endif

#ifndef RECENT_MODE
  #error You must define a value for RECENT_MODE
#endif

#if DEBUG
  #define debug_print(x) Serial.print(x)
  #define debug_println(x) Serial.println(x)
#else
  #define debug_print(x) do {} while(0)
  #define debug_println(x) do {} while(0)
#endif

#define DEBUG_ID(channelId) debug_print(F("DEBUG: ")); debug_print(channelId); debug_print(F(" - "));
#define boolstr(b) ((b) ? F("true") : F("false"))

// RECENT
#if RECENT_MODE == LL
  #define MAX_RECENT_SIZE 10 //! 10?
  // 439 -> 234
  // 457 -> 327
#elif RECENT_MODE == ARRAY
  #define RECENT_ARRAY_SIZE 1 //! 10-15?
  // 457 -> 157(?!)
  // 439 -> 163 (275 ~ 11 each)
  // 439 -> 175 -2> 163

  // 439? -> 163 -2> 163
  // 439 ->

  // 457 ->
#elif RECENT_MODE == EMA
  #define ALPHA 47.0
#else
  #error RECENT_MODE is defined as an invalid value (LL/ARRAY/EMA)
#endif

#define STUDENT_ID             "F120840"
#define IMPLEMENTED_EXTENSIONS F("UDCHARS,FREERAM,HCI,EEPROM,RECENT,NAMES,SCROLL")

#define BL_OFF   0
#define RED      1
#define GREEN    2
#define YELLOW   3
#define BLUE     4
#define PURPLE   5
#define TEAL     6
#define WHITE    7

#define SYNC_TIMEOUT   1000
#define SELECT_TIMEOUT 1000

#define TOP_LINE       0
#define BOTTOM_LINE    1

// column
#define ARROW_POSITION   0
#define ID_POSITION      1
#define DATA_POSITION    2
#define RECENT_POSITION  5
#define DESC_POSITION    10

#define displayTopChannel(ch)    displayChannel(TOP_LINE, ch)
#define displayBottomChannel(ch) displayChannel(BOTTOM_LINE, ch)

#define isCreateCommand(cmdId)  ((cmdId) == 'C')
#define isValueCommand(cmdId)   ((cmdId) == 'V' || (cmdId) == 'X' || (cmdId) == 'N')
#define isOutOfByteRange(value) ((value) < 0 || (value) > 255)

#define MAX_DESC_LENGTH  15
#define MAX_CMD_LENGTH   5

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

typedef enum {
  INITIALISATION,
  SYNCHRONISATION,
  AFTER_SYNC,
  MAIN_LOOP, // basically AWAITING_MESSAGE and AWAITING_PRESS
  SELECT_HELD,
  SELECT_AWAITING_RELEASE,
  BUTTON_PRESSED,
} State;

// used to pass info about previously read serial between methods
typedef struct serialinput_s {
  // len(CA123456789012345) = 17
  static constexpr byte INPUT_LEN = 17;

  char input[INPUT_LEN + 1] = "";
  byte inputLen = 0;
} SerialInput;

typedef enum {
  NORMAL,
  LEFT_MIN,
  RIGHT_MAX,
} HciState;

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
// singly-linked-list, impl. similar to Java's TreeSet
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

  void setDescription(const char *desc, byte descLen) {
    if (this->desc != nullptr)
      free((char*) this->desc);
    this->desc = desc;
    this->descLen = descLen;
    // SCROLL, reset scrolling
    scrollIndex = lastScrollTime = 0;
  }

private:
  // RECENT
#if RECENT_MODE == LL
  /*
  was difficult to think of how to impl RECENT!
  basic singly-linked-list with only tail addition (polling? is that the term)
  but with a max size, which once reached, adding will discard head value
  it's a Queue! FIFO
  */
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

  // debug - to help check if old recentHead gets deleted once reached max size
  bool _addedSixty = false;
  void _addSixtyRecentsOnce() {
    if (!_addedSixty) {
      for (uint i = 0; i < 60; i++)
        addRecent(random(0, 256));
      _addedSixty = true;
    }
  }

  // debug
  void _printAllRecent() const {
    debug_print(F("DEBUG: "));
    debug_print(id);
    debug_print(F(" r_len="));
    debug_print(recentLen);
    debug_print(F(", ["));

    if (recentHead == nullptr) {
      debug_println(']');
      return;
    }

    debug_print(recentHead->val);
    RecentNode *node = recentHead->next;
    while (node != nullptr) {
      debug_print(',');
      debug_print(node->val);
      node = node->next;
    }

    debug_println(']');
  }
#elif RECENT_MODE == ARRAY
  // circular array
  byte *recents = nullptr;
  ulong nRecents = 0;

  void addRecent(byte val) {
    if (recents == nullptr) {
      recents = (byte*) malloc(RECENT_ARRAY_SIZE * sizeof(*recents));
    }

    recents[nRecents % RECENT_ARRAY_SIZE] = val;
    nRecents++;
  }
#else
  /*
   * Use an exponential moving average (https://en.wikipedia.org/wiki/Moving_average#Exponential_moving_average)
   * inspired by answers to https://dsp.stackexchange.com/q/20333
   */
  byte data;
  double runningAvrg = 0;
  ulong nRecents = 0; // number of entered vals

  void addRecent(byte val) {
    data = val;

    // y[n] = α x[n] + (1−α) y[n−1]
    /*
     * before 64 values have been entered, each value will weigh: 1 / (nRecents + 1)
     *   so when nRecents == 0:
     *            weight = 1
     *         nRecents == 1:
     *            weight = 1/2
     *         etc.
     *   so runningAvrg should be very accurate/precise
     *
     * once 64 values have been entered, each value will weigh 1/64 which I think makes sense
     *
     * 1/46, 1/48, 1/44 are 'actual' best?
     * 1/47 is best, avrg diff of ~3.25 (2dp) - for high number of values
     *
     * mid 50s best for lower number ~100
     * midhigh 40s best for higher number ~3_000_000 / 300
     */
    // the weighting for the new value
    double alpha;
    if (nRecents < 64) {
      alpha = 1.0 / (nRecents + 1);
    } else {
      alpha = 1.0 / ALPHA;
    }

    runningAvrg = alpha * val + (1 - alpha) * runningAvrg;
    nRecents++;
  }
#endif

public:
  bool valueHasBeenSet() const {
#if RECENT_MODE == LL
    return recentTail != nullptr;
#elif RECENT_MODE == ARRAY
    return nRecents > 0;
#else
    return nRecents > 0;
#endif
  }

  void setData(byte value) {
    addRecent(value);
//! #if RECENT_MODE == LL
//     // _addSixtyRecentsOnce();//!
//     addRecent(value);
//     // _printAllRecent();//!
// #elif RECENT_MODE == ARRAY
//     addRecent(value);
// #else
//     data = value;
//     addRunningAvrg(value);
// #endif
  }

  byte getData() const {
#if RECENT_MODE == LL
    return recentTail == nullptr ? 0 : recentTail->val;
#elif RECENT_MODE == ARRAY
    return recents == nullptr ? 0 : recents[(nRecents - 1) % RECENT_ARRAY_SIZE];
#else
    return data;
#endif
  }

    // RECENT
  byte getAverageValue() const {
#if RECENT_MODE == LL
    if (recentHead == nullptr || recentLen == 0)
      return 0;

    uint sum = recentHead->val;
    RecentNode *node = recentHead->next;

    while (node != nullptr) {
      sum += node->val;
      node = node->next;
    }

    return round((float) sum / recentLen);
#elif RECENT_MODE == ARRAY
    if (recents == nullptr || nRecents == 0)
      return 0;

    uint sum = 0;
    const uint len = min(nRecents, RECENT_ARRAY_SIZE);

    for (int i = 0; i < len; i++) {
      sum += recents[i];
    }

    return round((float) sum / len);
#else
    return round(runningAvrg);
#endif
  }

  // HCI
  bool meetsHciRequirement(HciState hciState) const {
    switch (hciState) {
      case NORMAL:
        return true; // no requirement for NORMAL

      case LEFT_MIN:
        return valueHasBeenSet() && getData() < min;

      case RIGHT_MAX:
        return valueHasBeenSet() && getData() > max;
    }
  }


  static channel_s *headChannel;

  // these methods assume provided ID is valid (A-Z)
  static channel_s *create(char id, const char *desc, byte descLen);
  static channel_s *channelForId(char id);
  static channel_s *firstChannel(HciState hciState);
  static channel_s *channelBefore(const channel_s *ch, HciState hciState);
  static channel_s *channelAfter(const channel_s *ch, HciState hciState);
  static bool canGoUp(const channel_s *topChannel, HciState hciState);
  static bool canGoDown(const channel_s *topChannel, HciState hciState);
private:
  static void insertChannel(channel_s *ch);
} Channel;


/* function prototypes */
// reading & handling commands
void handleSerialInput(Channel **topChannelPtr);
void resetSerialInput(SerialInput &serialInput);
void printError(SerialInput &serialInput);
void processError(SerialInput &serialInput);
void createCommand(SerialInput &serialInput, Channel **topChannelPtr);
void valueCommand(SerialInput &serialInput);
void readValueCommand(char cmdId);
// display
void displayChannel(byte row, Channel *ch);
void displayRightJustified3Digits(uint num);
void clearChannelRow(byte row);
void updateDisplay(Channel *topChannel, HciState hciState);
void updateBacklight();
void selectDisplay();
// utils
void skipLine(Stream &s);
// debug
void _printChannelIds(const Channel *head);
void _printChannel(const Channel *ch, bool newLine = true);
void _printChannelsFull(Channel* head);
void _printHciState(HciState hciState);

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

  DEBUG_ID(id);
  if (ch == nullptr) {
    debug_print(F("new channel made, "));
    // don't need to free/delete because 'all channels will be used'
    ch = new Channel(id, desc, descLen);
    insertChannel(ch);
  } else {
    debug_print(F("updated "));
    ch->setDescription(desc, descLen);
  }
  debug_print(F("desc = {"));
  debug_print(desc);
  debug_println('}');

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

Channel *Channel::firstChannel(HciState hciState) {
  Channel *ch = headChannel;

  while (ch != nullptr) {
    if (ch->meetsHciRequirement(hciState))
      return ch;
    ch = ch->next;
  }

  return nullptr;
}

Channel *Channel::channelBefore(const Channel *ch, HciState hciState) {
  if (ch == nullptr)
    return nullptr;

  Channel *head = firstChannel(hciState);

  if (ch == head)
    return nullptr;

  Channel *prev = head;

  while (prev != nullptr) {
    Channel *next = channelAfter(prev, hciState);
    if (next == ch)
      return prev;
    prev = next;
  }

  return nullptr;
}

Channel *Channel::channelAfter(const Channel *ch, HciState hciState) {
  if (ch == nullptr)
    return nullptr;

  Channel *next = ch->next;

  if (hciState == NORMAL) {
    return next;
  }

  while (next != nullptr) {
    if (next->meetsHciRequirement(hciState))
      return next;
    next = next->next;
  }

  return nullptr;
}

bool Channel::canGoUp(const Channel *topChannel, HciState hciState) {
  if (topChannel == nullptr || topChannel == headChannel)
    return false;

  if (hciState == NORMAL)
    return true; // topChannel != headChannel

  Channel *node = headChannel;

  // try and find ANY node before topChannel that meets hciState's requirement
  while (node != nullptr) {
    if (node->meetsHciRequirement(hciState))
      return true;

    // early exit if reached topChannel
    if (node->next == topChannel)
      break;

    node = node->next;
  }

  return false;
}

bool Channel::canGoDown(const Channel *topChannel, HciState hciState) {
  // channelAfter & channelBefore handle nullptr
  Channel *btmChannel = channelAfter(topChannel, hciState);
  return channelAfter(btmChannel, hciState) != nullptr;
}

/* globals */
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

/* extensions */

/*
 * Arrows are 2 chevrons
 */
namespace UDCHARS {
  #define UP_ARROW_CHAR   0
  #define DOWN_ARROW_CHAR 1

  void createChars();
  void displayUpArrow(bool display);
  void displayDownArrow(bool display);

  void createChars() {
    byte upChevron[] PROGMEM = { B00100, B01010, B10001, B00100, B01010, B10001, B00000, B00000 };
    lcd.createChar(UP_ARROW_CHAR, upChevron);
    byte downChevron[] PROGMEM = { B00000, B10001, B01010, B00100, B10001, B01010, B00100, B00000 };
    lcd.createChar(DOWN_ARROW_CHAR, downChevron);
  }

  void displayUpArrow(bool display) {
    byte ch = display ? UP_ARROW_CHAR : ' ';
    lcd.setCursor(ARROW_POSITION, TOP_LINE);
    lcd.write(ch);
  }

  void displayDownArrow(bool display) {
    byte ch = display ? DOWN_ARROW_CHAR : ' ';
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
 *
 * Each channel occupies 26 bytes in EEPROM:
 * 1 id
 * 1 max
 * 1 min
 * 1 desc_length
 * 15 desc
 * 7 my student ID
 *
 * there are multiple ways to 'invalidate' a channel stored in the EEPROM:
 *  - update the stored student ID to not be my student ID
 *  -    "    "     "   channel ID to not be that channel's ID
 *  -    "    "     "   description length to be greater than 15
 *
 */
namespace _EEPROM {
  #define addressForId(id)        ((id - 'A') * 26)
  #define maxOffset(idAddr)       (idAddr + 1)
  #define minOffset(idAddr)       (idAddr + 2)
  #define descOffset(idAddr)      (idAddr + 3)
  #define studentIdOffset(idAddr) (idAddr + 19)

  void updateEEPROM(const Channel *ch);
  Channel *readEEPROM();

  namespace {
    void writeDesc(int offset, const char *desc, uint len) {
      EEPROM.update(offset, len);
      eeprom_update_block(desc, (void*) (offset + 1), len);
    }

    void readStr(int addr, char *str, uint len) {
      eeprom_read_block(str, (void*) addr, len);
      str[len] = '\0';
    }

    // check that the channel written was written by me by checking
    // that my student ID is written there
    bool channelHasValidStudentId(char id) {
      char readStudentId[8];
      readStr(studentIdOffset(addressForId(id)), readStudentId, 7);
      return strcmp(readStudentId, STUDENT_ID) == 0;
    }

    Channel *readChannel(char id) {
      if (!channelHasValidStudentId(id))
        return nullptr;

      const uint idAddr = addressForId(id);

      // basic verification using channel ID
      if (EEPROM[ idAddr ] != id)
        return nullptr;

      byte descLen =  EEPROM[ descOffset(idAddr) ];
      // if the stored description isn't valid
      if (descLen == 0 || descLen > MAX_DESC_LENGTH)
        return nullptr;

      char *desc = (char*) malloc((1 + descLen) * sizeof(*desc));
      readStr(descOffset(idAddr) + 1, desc, descLen);

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

    writeDesc(descOffset(addr), ch->desc, ch->descLen);
    // write my student ID next to channel data for persistence validation
    eeprom_update_block(STUDENT_ID, (void*) studentIdOffset(addr), 7);
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
  void _invalidateChannel(char id) {
    EEPROM.update(addressForId(id), 0);
  }

  // debug
  void _invalidateEEPROM() {
    for (char id = 'A'; id <= 'Z'; id++)
      EEPROM.update(addressForId(id), 0);
  }
}

// NAMES and SCROLL together
namespace NAMES_SCROLL {
  // scroll SCROLL_CHARS every SCROLL_TIMEOUT milliseconds
  #define SCROLL_CHARS      1
  #define SCROLL_TIMEOUT    500
  #define DESC_DISPLAY_LEN  6

  namespace {
    bool shouldScroll(Channel *ch) { // SCROLL
      return ch->descLen > DESC_DISPLAY_LEN
        && millis() - ch->lastScrollTime >= SCROLL_TIMEOUT;
    }
  }

  void displayChannelName(int row, Channel *ch);

  void displayChannelName(int row, Channel *ch) {
    // ch->desc SHOULD NOT be nullptr

    const byte dLen = ch->descLen;
    byte &si = ch->scrollIndex;

    // NAMES
    lcd.setCursor(DESC_POSITION, row);
    for (int i = si; i < si + DESC_DISPLAY_LEN; i++) {
      // display space(s) at end to overwrite old displayed value
      char c = (i < dLen) ? ch->desc[i] : ' ';
      lcd.print(c);
    }

    // SCROLL
    if (shouldScroll(ch)) {
      si += SCROLL_CHARS;
      // once full desc has been displayed, return to start
      if (si + DESC_DISPLAY_LEN > dLen + 1) { // +1 to make even lengths work (because of 'trailing' char)
        si = 0;
      }
      ch->lastScrollTime = millis();
    }
  }
}

void setup() {
  Serial.begin(9600);
  lcd.begin(16, 2);
  lcd.clear();
}

void loop() {
  static State state = INITIALISATION;
  static HciState hciState; // HCI
  static Channel *topChannel;
  static ulong selectPressTime;
  static byte pressedButton;

  switch (state) {
  case INITIALISATION:
    hciState = NORMAL;
    topChannel = Channel::headChannel = nullptr;
    selectPressTime = 0;
    pressedButton = 0;
    UDCHARS::createChars(); // UDCHARS
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
    topChannel = Channel::headChannel = _EEPROM::readEEPROM();
    debug_print(F("DEBUG: Channels stored in EEPROM: "));
    _printChannelsFull(topChannel);

    state = MAIN_LOOP;
    break;

  case MAIN_LOOP: // basically AWAITING_PRESS & AWAITING_MESSAGE
    pressedButton = lcd.readButtons();

    if (pressedButton & BUTTON_SELECT) {
      debug_println(F("DEBUG: SELECT pressed"));
      selectPressTime = millis();
      state = SELECT_HELD;
      pressedButton = 0;

    } else if (pressedButton & BUTTON_UP) {
      debug_println(F("DEBUG: UP pressed"));
      if (Channel::canGoUp(topChannel, hciState))
        topChannel = Channel::channelBefore(topChannel, hciState);
      pressedButton = BUTTON_UP;

    } else if (pressedButton & BUTTON_DOWN) {
      debug_println(F("DEBUG: DOWN pressed"));
      if (Channel::canGoDown(topChannel, hciState))
        topChannel = Channel::channelAfter(topChannel, hciState);
      pressedButton = BUTTON_DOWN;

    } else if (pressedButton & BUTTON_LEFT) {
      // HCI
      debug_println(F("DEBUG: LEFT pressed"));
      hciState = (hciState == LEFT_MIN) ? NORMAL : LEFT_MIN;
      _printHciState(hciState);
      // 'reset' topChannel (get first channel that meets hcistate)
      topChannel = Channel::firstChannel(hciState);
      pressedButton = BUTTON_LEFT;

    } else if (pressedButton & BUTTON_RIGHT) {
      // HCI
      debug_println(F("DEBUG: RIGHT pressed"));
      hciState = (hciState == RIGHT_MAX) ? NORMAL : RIGHT_MAX;
      _printHciState(hciState);
      // 'reset' topChannel
      topChannel = Channel::firstChannel(hciState);
      pressedButton = BUTTON_RIGHT;
    }

    if (pressedButton != 0)
      state = BUTTON_PRESSED;

    handleSerialInput(&topChannel);

    updateDisplay(topChannel, hciState);
    break;

  // select is currently being held, waiting to reach 1 second
  case SELECT_HELD:
    // if select has been held for 1 second
    if (millis() - selectPressTime >= SELECT_TIMEOUT) {
      debug_println(F("DEBUG: SELECT has been held for 1s"));
      lcd.clear();
      selectDisplay();
      state = SELECT_AWAITING_RELEASE;
    } else {
      // if select has been released
      if (!(lcd.readButtons() & BUTTON_SELECT)) {
        debug_println(F("DEBUG: SELECT released before 1s"));
        state = MAIN_LOOP;
      }
    }
    break;

  // wait until select is released
  case SELECT_AWAITING_RELEASE:
    // if SELECT has been released
    if (!(lcd.readButtons() & BUTTON_SELECT)) {
      debug_println(F("DEBUG: SELECT released"));
      lcd.clear();
      state = MAIN_LOOP;
    }

    handleSerialInput(&topChannel);
    break;

  case BUTTON_PRESSED:
    if (!(lcd.readButtons() & pressedButton)) {
      debug_println(F("DEBUG: button released"));
      pressedButton = 0;
      state = MAIN_LOOP;
    }

    handleSerialInput(&topChannel);
    break;
  }
}

/* reading commands */

void handleSerialInput(Channel **topChannelPtr) {
  static SerialInput serialInput;

  while (Serial.available()) {
    char *input = serialInput.input;
    byte &inputLen = serialInput.inputLen;

    char read = Serial.read();

    if (read == '\n') {

      input[inputLen] = '\0';

      if (inputLen < 3 || !isUpperCase(input[1]))
        printError(serialInput);
      else {
        if (isCreateCommand(input[0]))
          createCommand(serialInput, topChannelPtr);
        else if (isValueCommand(input[0]) && inputLen <= 5)
          valueCommand(serialInput);
        else
          printError(serialInput);
      }

      resetSerialInput(serialInput);

      break;
    }

    input[inputLen++] = read;

    if (inputLen > SerialInput::INPUT_LEN) {
      if (isCreateCommand(input[0]) && isUpperCase(input[1])) {
        skipLine(Serial);
        createCommand(serialInput, topChannelPtr);
      } else
        processError(serialInput);

      resetSerialInput(serialInput);

      break;
    }
  }
}

void resetSerialInput(SerialInput &serialInput) {
  memset(serialInput.input, '\0', SerialInput::INPUT_LEN + 1);
  serialInput.inputLen = 0;
}

void printError(SerialInput &serialInput) {
  Serial.print(F("ERROR:"));
  Serial.println(serialInput.input);
}

void processError(SerialInput &serialInput) {
  Serial.print(F("ERROR:"));
  Serial.print(serialInput.input);

  while (Serial.available()) {
    char read = Serial.read();
    Serial.print(read);
    if (read == '\n')
      break;
  }
}

void createCommand(SerialInput &serialInput, Channel **topChannelPtr) {
  char *input = serialInput.input;

  char channelId = input[1];

  byte descLen = serialInput.inputLen - 2;
  char *desc = (char*) malloc((1 + descLen) * sizeof(*desc));
  strncpy(desc, input + 2, descLen);
  desc[descLen] = '\0';

  Channel *ch = Channel::create(channelId, desc, descLen);

  // if creating first channel
  if (*topChannelPtr == nullptr) {
    debug_println(F("DEBUG: ^ was first channel"));
    *topChannelPtr = ch;
  }

  _EEPROM::updateEEPROM(ch);
}

void valueCommand(SerialInput &serialInput) {
  char *input = serialInput.input;

  char cmdId = input[0];
  char channelId = input[1];

  char *valueS = input + 2;

  char *temp;
  long value = strtol(valueS, &temp, 10);
  // message has invalid number
  if (*temp != '\0') {
    debug_println(F("DEBUG: Not numeric:"));
    return printError(serialInput);
  } else if (isOutOfByteRange(value)) {
    debug_println(F("DEBUG: Outside of 0-255:"));
    return printError(serialInput);
  }

  Channel *ch = Channel::channelForId(channelId);
  // if channel hasn't been created, don't do anything
  if (ch == nullptr)
    return;

  DEBUG_ID(channelId);
  debug_print(value);
  debug_print(F(" = "));

  switch (cmdId) {
    case 'V':
      debug_println(F("data"));
      ch->setData(value);
      break;
    case 'X':
      debug_println(F("max"));
      ch->max = value;
      break;
    case 'N':
      debug_println(F("min"));
      ch->min = value;
      break;
  }

  _EEPROM::updateEEPROM(ch);
}

/* display */

void displayChannel(byte row, Channel *ch) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(ch->id);
  lcd.setCursor(DATA_POSITION, row);
  // leave blank until value has been set
  if (ch->valueHasBeenSet())
    displayRightJustified3Digits(ch->getData());
  else
    lcd.print(F("   "));

  // RECENT
  lcd.setCursor(RECENT_POSITION, row);
  if (ch->valueHasBeenSet()) {
    lcd.print(',');
    displayRightJustified3Digits(ch->getAverageValue());
  } else
    lcd.print(F("    "));

  // NAMES,SCROLL
  NAMES_SCROLL::displayChannelName(row, ch);
}

void displayRightJustified3Digits(uint num) {
  char buf[4];
  sprintf(buf, "%3d", num);
  lcd.print(buf);
}

void clearChannelRow(byte row) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(F("               "));
}

void updateDisplay(Channel *topChannel, HciState hciState) {
  updateBacklight();

  // UDCHARS,HCI
  UDCHARS::displayUpArrow(Channel::canGoUp(topChannel, hciState));
  UDCHARS::displayDownArrow(Channel::canGoDown(topChannel, hciState));

  if (topChannel != nullptr) {
    displayTopChannel(topChannel);
  } else {
    clearChannelRow(TOP_LINE);
  }

  Channel *btmChannel = Channel::channelAfter(topChannel, hciState);
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

  Channel *ch = Channel::headChannel;

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

  lcd.setCursor(4, TOP_LINE);
  lcd.print(F(STUDENT_ID));

  // FREERAM
  FREERAM::displayFreeMemory();
}

/* Utility functions */

void skipLine(Stream &s) {
  s.find('\n');
}

// debug
void _printChannelIds(const Channel *head) {
  debug_print(F("DEBUG: channels=["));

  if (head == nullptr) {
    debug_println(']');
    return;
  }

  debug_print(head->id);
  Channel *node = head->next;
  while (node != nullptr) {
    debug_print(',');
    debug_print(node->id);
    node = node->next;
  }

  debug_println(']');
}

// debug
void _printChannel(const Channel *ch, bool newLine) {
  debug_print(F("DEBUG: Channel "));
  debug_print(ch->id);

  debug_print(F(", data="));
  debug_print(ch->getData());

  debug_print(F(", max="));
  debug_print(ch->max);

  debug_print(F(", min="));
  debug_print(ch->min);

  debug_print(F(", description={"));
  debug_print(ch->desc);
  debug_print('}');
  debug_print('@');
  debug_print((size_t) ch->desc);

  debug_print(F(", descLen="));
  debug_print(ch->descLen);

  if (newLine)
    debug_println();
}

void _printChannelsFull(Channel* head) {
  if (head == nullptr) {
    debug_println(F("[]"));
    return;
  }

  debug_println('[');

  _printChannel(head);
  Channel *node = head->next;
  while (node != nullptr) {
    _printChannel(node);
    node = node->next;
  }

  debug_println(F("DEBUG: ]"));
}

// debug HCI
void _printHciState(HciState hciState) {
  debug_print(F("DEBUG: HciState: "));
  if (hciState == NORMAL) {
    debug_println(F("NORMAL"));
  } else if (hciState == LEFT_MIN) {
    debug_println(F("LEFT_MIN"));
  } else {
    debug_println(F("RIGHT_MAX"));
  }
}
