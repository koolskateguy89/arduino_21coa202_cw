// TODO: put all in 1 file
// TODO: put in documentation that avrg on top & recent on bottom

#define MAX_SIZE 64

// showComma = false if there is no data value displayed on left

void addRecentValue(byte val);
void displayAverage(int row, bool showComma); // top row
void displayMostRecentValue(int row, bool showComma); // bottom row

// was difficult to think of how to impl RECENT!
// basic singly-linked-list with only tail addition (polling? is that the term)
// but with a max size, which once reached, head value is discarded
typedef struct node_s {
  byte val;
  struct node_s *next;
} RecentNode;

RecentNode *recentHead = nullptr;
RecentNode *recentTail = nullptr; // keep track of recentTail for O(1) insertion instead of O(n)

byte _recentLen = 0;

struct node_s *_newNode(byte val) {
  RecentNode *node = (RecentNode*) malloc(sizeof(*node));
  node->val = val;
  node->next = nullptr;
  return node;
}

void addRecentValue(byte val) {  // O(1)
  if (recentHead == nullptr) {
    recentHead = _newNode(val);
    recentTail = recentHead;
    _recentLen = 1;
    return;
  }

  // because we only care about the most recent MAX_SIZE values, we can discard (free)
  // the oldest value, like an LRU cache
  if (_recentLen == MAX_SIZE) {
    RecentNode *oldHead = recentHead;
    recentHead = recentHead->next;
    free(oldHead);
    _recentLen--;
  }

  RecentNode *node = _newNode(val);
  recentTail->next = node;
  recentTail = node;
  _recentLen++;
}

void displayMostRecentValue(int row, bool showComma) { // O(1)
  lcd.setCursor(RECENT_POSITION, row);
  if (showComma)
    lcd.print(',');

  lcd.setCursor(RECENT_POSITION + 1, row);
  //? TODO: rightjustify?
  String tailVal = rightJustify3Digits(recentTail == nullptr ? 0 : recentTail->val);
  // String tailVal = String(recentTail == nullptr ? -1 : recentTail->val);
  // rightPad(tailVal, 3);
  lcd.print(tailVal);
}

void displayAverage(int row, bool showComma) {
  lcd.setCursor(RECENT_POSITION, row);
  if (showComma)
    lcd.print(',');

  lcd.setCursor(RECENT_POSITION + 1, row);
  String avrgS = rightJustify3Digits(calculateAverage());
  lcd.print(avrgS);
}

int calculateAverage() { // O(n)
  if (recentHead == nullptr)
    return 0;

  RecentNode *node = recentHead->next;
  int sum = recentHead->val;

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
      addRecentValue(random(0, 256));
    done = true;
  }
}

// debug
void _printAll(Print &p) {
  p.print(F("DEBUG: r_len="));
  p.print(_recentLen);
  p.print(F(" ["));

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
