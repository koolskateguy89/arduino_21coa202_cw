---
title: 21COA202 Coursework
subtitle:
author: _id number_
date: Semester 2 / SAP
---

* *In this template delete all the text in italics and replace with your
  own as appropriate.*

* *Delete Semester 2 or SAP from the Date above as appropriate.*

* *Add your id number above*

* *There is no need to write an introduction here*

## FSMs

* *Describe the finite state machine at the centre of your
  implementation.  Show the states and the transitions.  Draw
  the states and transitions as a picture and include it here.*

* *Several ways to draw FSMS are described on the LEARN pages for the
  module.  See the FAQ.*

* *If there are other (sub) FSMs in your code then indicate those here.*

## Data structures

* *Describe the data structures you are using to implement the
  Coursework.  These could be types (structures, enums), classes and
  constants.  Also describe the variables that are instances of these
  classes or types.*

* *When you have functions to update the global data structures/store,
  list these with a sentence description of what each one does.*

## Debugging

* *if you have code used to help you debug that is now commented out or
  managed by C macros, then keep this in your submission.  If you have
  other things to say, then put them here.*

## Reflection

* *200–500 words of reflection on your code.  Include those things that
  don’t work as well as you would like and how you would fix them.*

## Extension Features {.unnumbered}

* *For each extension feature you have implemented describe the
  additional code and changes to your FSM .  Give examples of types,
  variables and code that is important.*

* *do not write anything here--put it in the subsections following.*

* ***Delete the text from Extension Features to here***

## UDCHARS

*Write about this extension*

## FREERAM

*Write about this extension*

## HCI

*Write about this extension*

## EEPROM

*Write about this extension*

## RECENT

*Write about this extension*

## NAMES

*Write about this extension*

## SCROLL

*Write about this extension*

## Submission

* *After following the instructions, **delete this section and ALL the subsequent sections from your report**. *

* Prepare the report as a PDF.*

### From Word source

If you have prepared this using the Word template then use the styles `Heading 1` and `Heading 2` for each section and subsection.  It should create a new page for each `Heading 1` and `Heading 2`.  Please check this is the case.

### From Markdown source

If you are preparing this in `markdown`, then I applaud you.  To convert to a PDF use the `pandoc` and LaTeX software (available from <https://pandoc.org/> and <https://tug.org/texlive/>).  `pandoc` is installed in the N001/2/3 labs under both MacOS and Windows.

~~~bash
pandoc -N -o output.pdf --template=coa202.latex input.md --shift-heading-level-by=-1
~~~

`coa202.latex` is available from LEARN.  This works for me with `pandoc` version 2.11.4.* and later versions.

### Gradescope Tagging

After deleting the sections from submission onwards there should be a tag on every page.  If you have an untagged page, then find a tag for it.  There are tags for the title page, data structure pages, fsms, testing and each extension.
