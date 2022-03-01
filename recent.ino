// TODO: put all in 1 file
// TODO: put in documentation that recent on top & average on bottom

#define MAX_SIZE 64

void addRecentValue(byte val);
void displayAverage(int row); // top row
void displayMostRecentValue(int row); // bottom row

// use a singly linked list to store most recent values
// was difficult to think of how to impl recent!
// basic singly-linked-list with only tail addition (polling?)
typedef struct node_s {
  byte val;
  struct node_s *next;
} Node;

Node *head = nullptr;
Node *tail = nullptr; // keep track of tail for O(1) insertion instead of O(n)

// should be kept 'up to date'
byte _len = 0;

static struct node_s *_newNode(byte val) {
  Node *node = (Node*) malloc(sizeof(*node));
  node->val = val;
  node->next = nullptr;
  return node;
}

void addRecentValue(byte val) {  // O(1)
  if (head == nullptr) {
    head = _newNode(val);
    tail = head;
    _len = 1;
    return;
  }

  // because we only care about the most recent MAX_SIZE values, we can discard (free)
  // the oldest value, like an LRU cache
  if (_len == MAX_SIZE) {
    Node *oldHead = head;
    head = head->next;
    free(oldHead);
    _len--;
  }

  Node *node = _newNode(val);
  tail->next = node;
  tail = node;
  _len++;
}

void displayMostRecentValue(int row) { // O(1)
  lcd.setCursor(RECENT_POSITION, row);
  lcd.print(',');
  //? TODO: rightjustify?
  String tailVal = String(tail == nullptr ? -1 : tail->val);
  rightPad(tailVal, 3);
  lcd.print(tailVal);
}

void _addSixtyOnce() {
  static bool done = false;

  if (!done) {
    for (byte i = 0; i < 60; i++)
      addRecentValue(random(0, 256));
    done = true;
  }
}

void displayAverage(int row) {  // O(n)
  lcd.setCursor(RECENT_POSITION, row);
  lcd.print(',');
  //? TODO: rightjustify?
  String avrgS = String(calculateAverage());
  rightPad(avrgS, 3);
  lcd.print(avrgS);

  if (DEBUG) _addSixtyOnce();
  if (DEBUG) _printAll(Serial);
}

int calculateAverage() {
  if (head == nullptr)
    return -1;

  Node *node = head->next;
  int sum = head->val;

  while (node != nullptr) {
    sum += node->val;
    node = node->next;
  }

  return sum / _len;
}

void _printAll(Print &p) {
  p.print(F("DEBUG: r_len="));
  p.print(_len);
  p.print(F(" ["));

  if (head == nullptr) {
    p.println(']');
    return;
  }

  p.print(head->val);
  Node *node = head->next;
  while (node != nullptr) {
    p.print(',');
    p.print(node->val);
    node = node->next;
  }

  p.println(']');
}
