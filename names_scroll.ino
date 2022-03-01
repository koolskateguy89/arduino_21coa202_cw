// TODO: put all in 1 file
// NAMES and SCROLL together here
// gonna try and implment names & scroll together here

#define SCROLL_CHARS 2
#define SCROLL_TIMEOUT 1000
#define DESC_DISPLAY_LEN 7

void displayChannelName(int row, Channel *ch);

// letting avrg take up all 3 chars
// NAMES,SCROLL
void displayChannelName(int row, Channel *ch) {
  const uint dLen = ch->description.length();
  const byte si = ch->scrollIndex;

  lcd.setCursor(DESC_POSITION, row);
  String textToDisplay = ch->description.substring(si, si + DESC_DISPLAY_LEN);
  rightPad(textToDisplay, DESC_DISPLAY_LEN);
  lcd.print(textToDisplay);

  ScrollState &state = ch->scrollState;

  // SCROLL
  switch (state) {
    case SCROLL_START:
      // only need to scroll if channel desc is too big
      if (dLen > DESC_DISPLAY_LEN)
        state = SCROLL_MID;

      break;

    case SCROLL_MID:
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

    // not really needed? can be handled in SCROLL_MID
    case SCROLL_END:
      ch->scrollIndex = 0;
      state = SCROLL_START;
      break;
  }
}
