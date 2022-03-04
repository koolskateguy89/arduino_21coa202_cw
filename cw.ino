#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>
// #include <EEPROM.h>
// #include <avr/eeprom.h> // TODO: EEPROM

#define STUDENT_ID F("    F120840     ")

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

#define ARROW_POSITION   0
#define ID_POSITION      1
#define DATA_POSITION    2
#define RECENT_POSITION  5
#define DESC_POSITION    10

#define IMPLEMENTED_EXTENSIONS F("UDCHARS,FREERAM,RECENT,NAMES,SCROLL")

#define isCreateCommand(cmdId) (cmdId == 'C')
#define isValueCommand(cmdId)  (cmdId == 'V' || cmdId == 'X' || cmdId == 'N')
#define isOutOfRange(value)    (value < 0 || value > 255)

#define MAX_DESC_LENGTH  15
#define MAX_CMD_LENGTH   5

// UDCHARS
#define hideTopArrow()    hideArrow(TOP_LINE)
#define hideBottomArrow() hideArrow(BOTTOM_LINE)


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

// only 465 bytes free when all 26 taken
// 360B sfter 20 valus entered
// 335 25
// 153B after > 64 values entered lets gooo
typedef struct channel_s {
  channel_s(char id) {
    this->id = id;
    this->setDescription("");
    this->data = this->min = 0;
    this->max = 255;
    this->next = nullptr;
  }

  char id;
  String description;
  byte data, max, min;
  struct channel_s *next;

  // SCROLL
  byte scrollIndex;
  ulong lastScrollTime;
  ScrollState scrollState;

  void setDescription(const String description) {
    this->description = description;
    // SCROLL
    // reset scrolling
    scrollIndex = lastScrollTime = 0;
    scrollState = SCROLL_START;
  }
} Channel;


/* function prototypes */
// main (is that gonna be state name?)
// reading commands (main)
void readCreateCommand(Channel **topChannel);
void readValueCommand(char cmdId);
void messageError(char cmdId, const String &cmd);
// handling button presses (main)
// TODO
// channels
Channel *createChannelImpl(char id, const String description);
Channel *channelForId(char id);
Channel *channelBefore(const Channel *ch);
Channel *getBottom(const Channel *topChannel);
bool channelHasBeenCreated(char id);
bool canGoUp(const Channel *topChannel);
bool canGoDown(const Channel *topChannel);
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
// UDCHARS
void showUpArrow(int row = TOP_LINE);
void showDownArrow(int row = BOTTOM_LINE);
void hideArrow(int row);
// FREERAM
void displayFreeMemory(int row = BOTTOM_LINE);
// EEPROM
void readEeprom();
void updateEEPROMDesc(char id, const String desc);
void updateEepromMax(char id, byte max);
void updateEepromMin(char id, byte min);
// RECENT
void addRecentValue(byte val);
void displayAverage(int row = TOP_LINE, bool showComma = true);
void displayMostRecentValue(int row = BOTTOM_LINE, bool showComma = true);
// NAMES,SCROLL
void displayChannelName(int row, Channel *ch);


/* globals */
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();
Channel *headChannel = nullptr;


void setup() {
  Serial.begin(9600);
  Serial.println(F("\nF120840"));
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
    topChannel = nullptr;
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

      if (isCreateCommand(cmdId)) {
        readCreateCommand(&topChannel);
        //! debug (remove once checked EEPROM)
        _printChannelsFull(Serial);
      } else if (isValueCommand(cmdId))
        readValueCommand(cmdId);
      else
        skipLine(Serial);
    }

    break;

  // may not need to be a state? cos really its not (but left/right are)
  //? make into a func?
  case UP_PRESSED:
    if (canGoUp(topChannel))
      topChannel = channelBefore(topChannel);
    state = MAIN;
    break;

  // may not need to be a state? cos really its not
  //? make into a func?
  case DOWN_PRESSED:
    if (canGoDown(topChannel))
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

void _insertChannel(Channel *ch) {
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
Channel *createChannelImpl(char id, const String description) {
  Channel *ch = channelForId(id);

  if (ch == nullptr) {
    Serial.print(F("DEBUG: Making new channel with id "));
    Serial.println(id);
    // shouldn't free/delete because 'all channels will be used'
    ch = new Channel(id);
    _insertChannel(ch);
  }

  ch->setDescription(description);
  return ch;
}

// if the channel hasn't been created, returns nullptr
Channel *channelForId(char id) {
  Channel *node = headChannel;

  while (node != nullptr) {
    if (node->id == id)
      return node;
    node = node->next;
  }

  return nullptr;
}

Channel *channelBefore(const Channel *ch) {
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

Channel *getBottom(const Channel *topChannel) {
  return topChannel == nullptr ? nullptr : topChannel->next;
}

bool channelHasBeenCreated(char id) {
  return channelForId(id) != nullptr;
}

bool canGoUp(const Channel *topChannel) {
  return topChannel != headChannel;
}

bool canGoDown(const Channel *topChannel) {
  return topChannel != nullptr && topChannel->next != nullptr && topChannel->next->next != nullptr;
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
void _printChannel(Print &p, const Channel *ch, bool newLine = true);
void _printChannel(Print &p, const Channel *ch, bool newLine) {
  p.print(F("DEBUG: Channel "));
  p.print(ch->id);

  p.print(F(", data="));
  p.print(ch->data);

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
    p.println(']');
    return;
  }

  _printChannel(Serial, headChannel);
  Channel *node = headChannel->next;
  while (node != nullptr) {
    _printChannel(Serial, node);
    node = node->next;
  }

  p.println(']');
}

/* reading commands */

// for some reason Serial sometimes says 0 chars available even tho
// some have been sent over
// Serial.flush before reading seems to fix this
//! NOT ANYMORE! IT JUST DOESNT WANT TO CONSITENTLY WORK NOR NOT WORK
//* it works fine with readStringUntil
void readCreateCommand(Channel **topChannel) {
  String cmd = Serial.readStringUntil('\n');

  char channelId = cmd[0];
  //! TODO: check isUpper(channelId)
  String description = cmd.substring(1, 1 + MAX_DESC_LENGTH);
  if (description.length() == 0) {
    messageError('C', cmd);
    return;
  }

  Serial.println("desc: [" + description + "]");
  createChannelImpl(channelId, description);

  // if creating first channel
  if (*topChannel == nullptr) {
    Serial.println(F("DEBUG: FIRST CHANNEL MADE"));
    *topChannel = headChannel;
  }

  /*
  Serial.flush();

  Serial.println(Serial.available());

  char channelId = Serial.available() ? Serial.read() : ' ';
  //char channelId = Serial.read();
  Serial.print(F("chId: ["));
  Serial.print(channelId);
  Serial.println(']');

  // channelId has to be an uppercase letter
  if (!isUpperCase(channelId)) {
    skipLine(Serial);
    return;
  }

  String description = "";
  // description.reserve(MAX_DESC_LENGTH);

  Serial.flush();

  Serial.println("Valid channel ID");
  if (!Serial.available()) {
    Serial.println(F("NONE AVAILABLE"));
    Serial.print(F("RecN: ["));
    Serial.print(Serial.readString());
    Serial.println(']');
    return;
  }

  Serial.flush();

  // continue using Serial.read() until == '\n' or -1
  while (Serial.available()) {
    char c;
    if ((c = Serial.read()) == '\n') {
      Serial.println(" \n");
      break;
    }
    Serial.print(c);

    description += c;
    if (description.length() >= MAX_DESC_LENGTH) {
      Serial.println("Too long");
      skipLine(Serial);
      break;
    }
  }

  if (description.length() == 0)
    return;

  Serial.println("desc: [" + description + "]");
  createChannelImpl(channelId, description);

  // if creating first channel
  if (*topChannel == nullptr) {
    Serial.println(F("DEBUG: FIRST CHANNEL MADE"));
    *topChannel = headChannel;
  }
  */
}

void readValueCommand(char cmdId) {
  String cmd = Serial.readStringUntil('\n');

  if (cmd.length() < 2 || cmd.length() > 4) {
    messageError(cmd);
    return;
  }

  char channelId = cmd[0];
  String valueS = cmd.substring(1);
  long value = valueS.toInt();

  if (!isUpper(channelId)
      || (value == 0 && valueS != "0") // input wasn't numeric
      || isOutOfRange(value)
     ) {
    messageError(cmd);
    return;
  }

  Channel *ch = channelForId(channelId);
  // if channel hasn't been created, don't do anything
  if (ch == nullptr)
    return;

  if (cmdId == 'V') {
    addRecentValue(value);
    ch->data = value;
  } else if (cmdId == 'X')
    ch->max = value;
  else
    ch->min = value;

  /*
  Serial.flush();

  char channelId = Serial.available() ? Serial.read() : ' ';
  // channelId has to be an uppercase letter
  if (!isUpperCase(channelId)) {
    skipLine(Serial);
    return;
  }

  uint value = 0;
  int i = 0;

  Serial.flush();

  while (Serial.available()) {
    char c = Serial.read();
    if (c == '\n')
      break;
    if (i >= MAX_CMD_LENGTH - 2 || !isDigit(c)) {
      i = -1;
      skipLine(Serial);
      break;
    }

    value *= 10;
    value += c - '0';
    i++;
  }

  if (i != -1 && 0 <= value && value <= 255) {
    Channel *ch = channelForId(channelId);
    // if channel hasn't been created
    if (ch == nullptr)
      return;

    if (cmdId == 'V') {
      addRecentValue(value);
      ch->data = value;
    } else if (cmdId == 'X')
      ch->max = value;
    else
      ch->min = value;
  }
  */
}

void messageError(char cmdId, const String &cmd) {
  Serial.print(F("ERROR: "));
  Serial.print(cmdId);
  Serial.println(cmd);
  // Serial.print(F(", length="));
  // Serial.println(1 + cmd.length());
}

/* display */

void displayChannel(uint8_t row, Channel *ch) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(ch->id);
  lcd.setCursor(DATA_POSITION, row);
  lcd.print(rightJustify3Digits(ch->data));

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
  if (canGoUp(topChannel))
    showUpArrow();
  else
    hideTopArrow();
  if (canGoDown(topChannel))
    showDownArrow();
  else
    hideBottomArrow();

  if (topChannel != nullptr) {
    displayChannel(TOP_LINE, topChannel);
  } else {
    clearChannelRow(TOP_LINE);
  }

  Channel *const btmChannel = getBottom(topChannel);
  if (btmChannel != nullptr) {
    displayChannel(BOTTOM_LINE, btmChannel);
  } else {
    clearChannelRow(BOTTOM_LINE);
  }

  // RECENT
  displayAverage(TOP_LINE, topChannel != nullptr);
  displayMostRecentValue(BOTTOM_LINE, btmChannel != nullptr);
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
    //! update once module leader answers questions
    if (ch->data > ch->max)
      color |= RED;
    else if (ch->data < ch->min)
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
  displayFreeMemory();
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
