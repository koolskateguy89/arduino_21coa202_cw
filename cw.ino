#include <Wire.h>
#include <Adafruit_RGBLCDShield.h>
#include <utility/Adafruit_MCP23017.h>

#define NCOLORS  7
#define RED    0x1
#define GREEN  0x2
#define YELLOW 0x3
#define BLUE   0x4
#define PURPLE 0x5
#define TEAL   0x6
#define WHITE  0x7

#define TOP_LINE       0
#define BOTTOM_LINE    1
#define TOP_CURSOR     0, TOP_LINE
#define BOTTOM_CURSOR  0, BOTTOM_LINE

#define ARROW_POSITION   0
#define ID_POSITION      1
#define DATA_POSITION    2
#define RECENT_POSITION  5
#define DESC_POSITION    9

#define IMPLEMENTED_EXTENSIONS F("UDCHARS,FREERAM,RECENT,NAMES,SCROLL")

#define hideTopArrow() hideArrow(TOP_LINE)
#define hideBottomArrow() hideArrow(BOTTOM_LINE)

#define DEBUG 0

// currently, static = doesn't use any globals

/* data types */
typedef unsigned int uint;
typedef unsigned long ulong;

typedef enum { // TODO: finish
  INITIALISATION,
  SYNCHRONISATION,
  AFTER_SYNC,
  MAIN, // main might have to be its own FSM (because of different types of messages)
  UP_PRESSED,
  DOWN_PRESSED,
  SELECT_IS_HELD, // TODO: better name
  SELECT_AWAITING_RELEASE,
  TODO,
} State;

typedef enum {
  SCROLL_START, // scrollIndex == 0
  SCROLL_MID,   // scrollIndex++
  SCROLL_END,   // scrollIndex = 0
} ScrollState;

/*
Benefits of implementing channels as a LL instead of array[26]:
- better memory usage as memory for a channel will be allocated on demand
- easier to get prev/next channel if some channels haven't been created
impl notes:
- creating new channel will just insert it between 2 nodes
  (think of insertion sort)
*/
// singly-linked-list, impl. similar to a TreeSet<Byte> (Java)
// takes 21 bytes, * 26
// takes 21 - ~103 bytes
typedef struct channel_s {
  char id;
  String description; // char desc[15];
  byte data, max, min;

  // SCROLL
  byte scrollIndex;
  ulong lastScrollTime;
  ScrollState scrollState;

  struct channel_s *next;
} Channel;
Channel *headChannel = nullptr;

/* function prototypes */
// main (is that gonna be state name?)
static bool commandDoesNotConform(const String &cmd);
static bool isValidCmdId(char id);
void messageError(const String &cmd);
// channels
Channel *createChannelImpl(char id, const String description);
Channel *channelForId(char id);
Channel *channelBefore(const Channel *ch);
bool channelHasBeenCreated(char id);
static Channel *getBottom(const Channel *topChannel);
static bool canGoUp(const Channel *topChannel);
static bool canGoDown(const Channel *topChannel);
// display
void displayChannel(int row, Channel *ch);
void clearChannelRow(int row);
void normalDisplay(Channel* topChannel);
void selectDisplay();
// utils
static String rightJustify(int num);
static void rightPad(String &str, int desiredLen);

/* extensions */
// UDCHARS
void showUpArrow(int row = TOP_LINE);
void showDownArrow(int row = BOTTOM_LINE);
void hideArrow(int row);
// FREERAM
void displayFreeMemory(int row = BOTTOM_LINE);
// RECENT
void addRecentValue(byte val);
void displayAverage(int row = TOP_LINE);
void displayMostRecentValue(int row = BOTTOM_LINE);
// NAMES,SCROLL
// void displayChannelName(int row, Channel& ch);
void displayChannelName(int row, Channel *ch);

/* globals */
Adafruit_RGBLCDShield lcd = Adafruit_RGBLCDShield();

void setup() {
  Serial.begin(9600);
  Serial.println(F("F120840"));
  lcd.begin(16, 2);
  lcd.clear();
}

void loop() {
  static State state = INITIALISATION;
  static Channel *topChannel; // btmChannel = topChannel->next
  static ulong selectPressTime;

  int b;

  switch (state) {
  case INITIALISATION:
    // state = TODO;
    topChannel = nullptr;
    selectPressTime = 0;

    state = SYNCHRONISATION;
    break;

  case SYNCHRONISATION:
    static ulong lastQTime = 0;

    lcd.setBacklight(PURPLE);

    // 1s timeout
    if (millis() - lastQTime >= 1000) {
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
    //Serial.println(F("DEBUG: Awaiting button press"));

    normalDisplay(topChannel);
    //normalDisplay(channels);

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
    }

    //? make another state READING_MESSAGE?
    if (Serial.available()) {
      /*
      TODO: I don't want to use readString() in case of very large description
      that gets ignored, want to read a char at a time basically. That also accommodates
      for when multiple messages are sent quickly. But then I can't basically pre-verify
      that the message is valid and I shouldn't ignore it

      can maybe use
        Serial.parseInt() for V, X, N
      or make my own impl:
        n += read(), if more -> n *= 10 -> n+= read, etc

      might not need to cos readStringUntil handles spamming, but maybe memory problem if
      VERY long string is sent
       */

      String cmd = Serial.readStringUntil('\n');
      cmd.replace("\n", "");

      Serial.print(F("DEBUG: received: ["));
      Serial.print(F("DEBUG: received: length="));
      Serial.print(cmd.length());
      Serial.print(F(", ["));
      Serial.print(cmd);
      Serial.println(']');

      if (commandDoesNotConform(cmd)) {
        messageError(cmd);
        break;
      }

      char id = cmd[0];
      char channelId = cmd[1];
      bool created = channelHasBeenCreated(channelId);

      if (id == 'C') {
        Serial.print(F("DEBUG: Try create channel: "));
        Serial.print(channelId);
        Serial.print(F(" @ "));
        Serial.println(millis());
        String description = cmd.substring(2).substring(0, 15);

        //! should make this a state: WAITING_FOR_FIRST_CHANNEL
        bool makingFirstChannel = headChannel == nullptr;
        createChannelImpl(channelId, description);
        if (makingFirstChannel)
          topChannel = headChannel;
      } else { // V X N
        int value = cmd.substring(2).toInt();
        if (value < 0 || value > 255) {
          messageError(cmd);
          break;
        }

        Serial.print(F("DEBUG: Try set value="));
        Serial.println(value);
        Serial.print(F("DEBUG: Channel "));
        Serial.print(channelId);

        Channel *ch = channelForId(channelId);

        if (created) {
          Serial.println(F(" HAS been created"));
          if (id == 'V') {
            addRecentValue(value);
            ch->data = value;
          } else if (id == 'X') {
            ch->max = value;
          } else {
            ch->min = value;
          }
        } else {
          Serial.println(F(" has NOT been created"));
        }
      }

      //printChannel(ch);
    }

    break;

  case UP_PRESSED:
    if (canGoUp(topChannel))
      topChannel = channelBefore(topChannel);
    state = MAIN;
    break;

  case DOWN_PRESSED:
    if (canGoDown(topChannel))
      topChannel = topChannel->next;
    state = MAIN;
    break;

  case SELECT_IS_HELD:  // select is currently being held, waiting to reach 1 second
    // if select has been held for 1 second
    if (millis() - selectPressTime >= 1000) {
      Serial.println(F("DEBUG: SELECT has been held for 1s"));
      lcd.clear();
      selectDisplay();
      state = SELECT_AWAITING_RELEASE;
    } else {
      // if select has been released
      if (!(lcd.readButtons() & BUTTON_SELECT)) {
        Serial.println(F("DEBUG: SELECT released before 1s"));
        state = MAIN; // might need like sub-state is main own FSM...
      } else {
        Serial.println(F("DEBUG: Timeout until SELECT held for 1s"));
      }
    }
    break;

  case SELECT_AWAITING_RELEASE:  // select is currently being held (has already been held for 1+ second
    Serial.println(F("DEBUG: Awaiting SELECT release"));
    if (!(lcd.readButtons() & BUTTON_SELECT)) {
      Serial.println(F("DEBUG: SELECT released"));
      lcd.clear();
      //? might need to store like a sub-state if main is its own FSM?
      // normalDisplay is set in main
      state = MAIN;
    }
    break;

  case TODO:
    /*
    static int i = 0;
    static char id = 'A';
    static bool top = true;

    Channel& ch = channels[i];
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

void _setChDesc(Channel *ch, const String description) {
  ch->description = description;
  // reset scrolling
  ch->scrollIndex = ch->lastScrollTime = 0;
  ch->scrollState = SCROLL_START;
}

// create new channel if not already created, else use new description
Channel *createChannelImpl(char id, const String description) {
  Channel *ch = channelForId(id);

  if (ch == nullptr) {
    Serial.print(F("DEBUG: Making new channel with id "));
    Serial.println(id);
    // shouldn't free because 'all channels will be used'
    ch = (Channel*) malloc(sizeof(*ch));
    ch->id = id;
    ch->data = ch->min = 0;
    ch->max = 255;
    _insertChannel(ch);
  }

  _setChDesc(ch, description);
  _printChannels(Serial);
  return ch;
}

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

bool channelHasBeenCreated(char id) {
  return channelForId(id) != nullptr;
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
void _printChannel(Print &p, const Channel *ch) {
  p.print(F("DEBUG: Channel "));
  p.print(ch->id);

  // p.print(F(": id="));
  // p.print(ch->id);

  p.print(F(", data="));
  p.print(ch->data);

  p.print(F(", max="));
  p.print(ch->max);

  p.print(F(", min="));
  p.print(ch->min);

  p.print(F(", description=["));
  p.print(ch->description);
  p.println(']');
}

static Channel *getBottom(const Channel *topChannel) {
  return topChannel == nullptr ? nullptr : topChannel->next;
}

static bool canGoUp(const Channel *topChannel) {
  return topChannel != headChannel;
}

static bool canGoDown(const Channel *topChannel) {
  return topChannel != nullptr && topChannel->next != nullptr && topChannel->next->next != nullptr;
}

// checks if a given command conforms to the protocol
static bool commandDoesNotConform(const String &cmd) {
  if (cmd.length() < 3)
    return true;

  // handles whitespace
  if (!isValidCmdId(cmd[0]))
    return true;

  // handles whitespace
  if (!isUpperCase(cmd[1]))
    return true;

  // max len is 5 (cmdId, channelId, <=3-digit number)
  if (cmd[0] != 'C' && cmd.length() > 5)
    return true;

  if (cmd[0] != 'C') {
    // after the channel ID, the rest of the message has to be numeric
    // handles whitespace
    for (int i = 2; char ch = cmd[i]; i++) {
      if (!isDigit(ch))
        return true;
    }
  }

  // TODO: all other cases for non-conforming commands

  return false;
}

static bool isValidCmdId(char id) {
  switch (id) {
  case 'C': case 'V': case 'X': case 'N':
    return true;

  default:
    return false;
  }
}

void messageError(const String &cmd) {
  Serial.print(F("ERROR: {"));
  Serial.print(cmd);
  Serial.print(F("}, length="));
  Serial.println(cmd.length());
}

// ch isn't const because of scrolling
void displayChannel(int row, Channel *ch) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(ch->id);
  // lcd.setCursor(DATA_POSITION, row);
  lcd.print(rightJustify(ch->data));

  // NAMES
  displayChannelName(row, ch);
}

void clearChannelRow(int row) {
  lcd.setCursor(ID_POSITION, row);
  lcd.print(F("    "));
}

void normalDisplay(Channel *const topChannel) {
  lcd.setBacklight(WHITE); // TODO: depends on values in current channels!

  // UDCHARS
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
  displayMostRecentValue();
  displayAverage();
}

void selectDisplay() {
  lcd.setBacklight(PURPLE);

  lcd.setCursor(TOP_CURSOR);
  lcd.print(F("F120840"));

  // FREERAM
  displayFreeMemory();
}

/* Utility functions */

static String rightJustify(int num) {
  if (num >= 100)
    return String(num);

  String prefix = (num >= 10) ? F(" ") : F("  ");
  prefix.concat(num);
  return prefix;
}

// pad spaces to the right of given string, usually to help overwrite old values
static void rightPad(String &str, int desiredLen) {
  int diff = desiredLen - str.length();
  while (diff > 0) {
    str.concat(' ');
    diff--;
  }
}
