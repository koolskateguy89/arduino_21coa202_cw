// TODO: put all in 1 file
// TODO: put in documentation that avrg on top & recent on bottom

#define MAX_SIZE 64

// showComma = false if there is no data value displayed on left

void addRecentValue(byte val);
void displayAverage(int row, bool display); // top row
void displayMostRecentValue(int row, bool display); // bottom row
uint calculateAverage();

// TODO: make this into a namespace?
namespace RECENT {

}

// was difficult to think of how to impl RECENT!
// basic singly-linked-list with only tail addition (polling? is that the term)
// but with a max size, which once reached, head value is discarded
typedef struct node_s {
  node_s(byte val) {
    this->val = val;
    this->next = nullptr;
  }

  byte val;
  struct node_s *next;
} RecentNode;

RecentNode *recentHead = nullptr;
RecentNode *recentTail = nullptr; // keep track of recentTail for O(1) insertion instead of O(n)

size_t _recentLen = 0;

void addRecentValue(byte val) {  // O(1)
  if (recentHead == nullptr) {
    recentHead = recentTail = new RecentNode(val);
    _recentLen = 1;
    return;
  }

  // because we only care about the most recent MAX_SIZE values, we can discard (delete)
  // the oldest value, like an LRU cache
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

void displayMostRecentValue(int row, bool display) { // O(1)
  display &= recentTail != nullptr;

  lcd.setCursor(RECENT_POSITION, row);
  lcd.print(display ? ',' : ' ');

  String tailVal = display ? rightJustify3Digits(recentTail->val) : F("   ");
  lcd.print(tailVal);
}

void displayAverage(int row, bool display) {
  display &= recentHead != nullptr;

  lcd.setCursor(RECENT_POSITION, row);
  lcd.print(display ? ',' : ' ');

  String avrg = display ? rightJustify3Digits(calculateAverage()) : F("   ");
  lcd.print(avrg);
}

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
