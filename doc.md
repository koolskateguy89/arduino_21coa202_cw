---
title: 21COA202 Coursework
subtitle:
author: _F120840_
date: Semester 2
---

**_In this template delete all the text in italics and replace with your
  own as appropriate._**

## FSMs

* *Describe the finite state machine at the centre of your
  implementation.  Show the states and the transitions.  Draw
  the states and transitions as a picture and include it here.*

- INITIALISATION
  - setup
- SYNCHRONISATION
  - synchronising through Serial
- AFTER_SYNC
  - intermediate step after sync and b4 main
- MAIN
  - main loop
- UP_PRESSED
  - waiting for up button to be released
- DOWN_PRESSED
  - waiting for down button to be released
- SELECT_IS_HELD
  - waiting until select has been held for 1s
- SELECT_AWAITING_RELEASE
  - waiting for select button to be released

![Main FSM alt text](main_fsm.png "Main FSM")

* *If there are other (sub) FSMs in your code then indicate those here.*

There are other FSMs in my code, they are in extensions. (tryna say to look at extensions part).

## Data structures

* *Describe the data structures you are using to implement the
  Coursework.  These could be types (structures, enums), classes and
  constants.  Also describe the variables that are instances of these
  classes or types.*

Channels are implemented as an ordered singly-linked-list (ordered by channel ID) of structs (each channel is a node). A single channel is made up of:

| Type | Name | Description |
| --- | --- | --- |
| char | id | This channel's ID (A-Z) |
| const char* | desc | This channel's description/name/title (max. 15 chars) |
| byte | descLen | The length of this channel's description |
| byte | max | This channel's maximum |
| byte | min | This channel's minimum |
| channel_s* | next | Pointer to the next created channel (by ID) |
| byte | scrollIndex | SCROLL: the start index of the currently displayed description |;
| unsigned long | lastScrollTime | SCROLL: the time the description was last scrolled |;
| ScrollState | scrollState | SCROLL: the current state of scrolling this channel's description |
| RecentNode* | recentHead | RECENT: the head of the RECENT linked list for this channel |
| RecentNode* | recentTail | RECENT: the tail of the RECENT linked list for this channel |
| byte | recentLen | RECENT: the number of recent values currently stored |

The head of this linked list is stored statically in `Channel::headChannel` and globally (as a reference) in `headChannel`. When a new channel is created using `Channel::create`, the linked list is updated using `Channel::insertChannel`, which will insert the new channel into the appropriate position, according to the channel's ID.

* *When you have functions to update the global data structures/store,
  list these with a sentence description of what each one does.*

| Function | Description |
| --- | --- |
| `Channel::create(char id, const char* desc, byte descLen)` | Creates a new channel with the provided ID and description if not already created, else updates description of channel |
| `Channel::insertChannel(Channel* ch)` | Inserts the given channel into the linked list of channels in its appropriate position |

## Debugging

* *if you have code used to help you debug that is now commented out or
  managed by C macros, then keep this in your submission.  If you have
  other things to say, then put them here.*

Debug functions generally start with '`_`', and are commented with '`// debug`'.

## Reflection

* *200–500 words of reflection on your code.  Include those things that
  don’t work as well as you would like and how you would fix them.*

I am very happy with my code. I think I thought of some ingenious solutions to some problems, such as managing the 64 most recent values and managing channels - using linkedlists is more memory friendly, at least initially, than using arrays. I believe everything works as desired.

Though I am unhappy that I mixed C & C++ constructs and didn't necessarily try to stick to one.

## Extension Features {.unnumbered}

* *For each extension feature you have implemented describe the
  additional code and changes to your FSM .  Give examples of types,
  variables and code that is important.*

* *do not write anything here--put it in the subsections following.*

* ***Delete the text from Extension Features to here***

## UDCHARS

*Write about this extension*

The arrows are 2 chevrons pointing in the appropriate direction. Lines ? and ? define the chars.

It was simple to implement as it was something done very early.

## FREERAM

*Write about this extension*

*In documentation, indicate which lines display the free SRAM*

Lines ?-? display the amount of free SRAM.

It was simple to implement as it was done in the Week 3 lab.

## HCI

*Write about this extension*

*In documentation, show LoC and thinking behind mechanism to display subsets of list of channels*

HCI is implemented using a finite state machine:

![HCI FSM alt text](hci_fsm.png "HCI FSM")

## EEPROM

*In documentation, indicate how lay out use of EEPROM, which LoC and functions I use to store information*

Each channel occupies 19 bytes in the EEPROM:

| # of Bytes | Description |
| ----- | --- |
| 1 | ID |
| 1 | Maximum value |
| 1 | Minimum Vaue |
| 1 | Description length |
| 15 | Description |

The beginning address for a channel is calculated using `(id - 'A') * 19`. This creates a distance of 19 bytes between each channel (A: 0, B: 19, C: 38, etc.).

A channel stored in EEPROM is 'validated' by checking the ID written at the address for the channel is correct and that the written description length does not exceed the maximum. Though this is a weak way to check that the channel was actually written and not just 'there', I couldn't think of a better way.

I use the namespace `_EEPROM` for all functions relating to using the EEPROM, namely `_EEPROM::updateEEPROM(Channel*)` and `_EEPROM::readEEPROM()`.

## RECENT

*In documentation, indicate the names and locations of the data structures used to store the recent values*

*running sum?*

I use a queue (implemented with a singly-linked-list), with a max size of 64, which once exceeded will discard the head value to manage the most recent values for a channel.

`Channel::recentHead` stores the head of this linkedlist, and can be used for all list-related operations.

Alternativity, this could be implemented using a circular byte array (`byte[64]`) and keeping track of:
- the index of the oldest value
- the number of values that have been entered

e.g.

```arduino
byte recents[64] = {0}; // initialiser to zero entire array
byte nRecents = 0; // max 64
byte oldestIndex = 0; // 0-63

void addRecent(byte val) {
  if (nRecents == 64) {
    // once max size has been reached, overwrite oldest value
    recents[oldestIndex] = val;
    oldestIndex++;
    // loop back to start once past end
    oldestIndex %= 64;
  } else {
    recents[nRecents] = val;
    nRecents++;
  }
}

unsigned int calculateAverage() {
  unsigned int sum = 0;

  for (byte i = 0; i < nRecents; i++) {
    sum += recents[i];
  }

  return round(sum / nRecents);
}
```

But I think using a linked-list should be more memory-efficient, at least for a small number of entered values.

## NAMES

*Write about this extension*

*In documentation, indicate data structure used to store the channel name and how it is printed to the display*

indicate data structure used to store the channel name and how it is printed to the display

A channel's description/name is stored in a `const char*`.It is printed to the display using `lcd.print` with padded spaces on the end to overwrite the description previously displayed.

## SCROLL

*Write about this extension*

*In documentation, highlight parts of FSM required for this particular requirement and the LoC and functions that carry this implementation*

SCROLL and NAMES are implemented together as they go hand-in-hand.

It is implemented using a simple FSM:

![SCROLL FSM alt text](scroll_fsm.png "SCROLL FSM")


## Submission

* *After following the instructions, **delete this section and ALL the subsequent sections from your report**.*

* *Prepare the report as a PDF.*

### From Markdown source

If you are preparing this in `markdown`, then I applaud you.  To convert to a PDF use the `pandoc` and LaTeX software (available from <https://pandoc.org/> and <https://tug.org/texlive/>).  `pandoc` is installed in the N001/2/3 labs under both MacOS and Windows.

~~~bash
pandoc -N -o output.pdf --template=coa202.latex input.md --shift-heading-level-by=-1
~~~

`coa202.latex` is available from LEARN.  This works for me with `pandoc` version 2.11.4.* and later versions.

### Gradescope Tagging

After deleting the sections from submission onwards there should be a tag on every page.  If you have an untagged page, then find a tag for it.  There are tags for the title page, data structure pages, fsms, testing and each extension.
