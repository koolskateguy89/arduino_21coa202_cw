// TODO: put all in 1 file

void showUpArrow(int row);
void showDownArrow(int row);
void hideArrow(int row);

void showUpArrow(int row) {
  byte upChevron[] = { B00100, B01010, B10001, B00100, B01010, B10001, B00000, B00000 };
  lcd.createChar(0, upChevron);
  lcd.setCursor(ARROW_POSITION, row);
  lcd.write(0);
}

void showDownArrow(int row) {
  byte downChevron[] = { B00000, B10001, B01010, B00100, B10001, B01010, B00100, B00000 };
  lcd.createChar(1, downChevron);
  lcd.setCursor(ARROW_POSITION, row);
  lcd.write(1);
}

void hideArrow(int row) {
  lcd.setCursor(ARROW_POSITION, row);
  lcd.print(' ');
}
