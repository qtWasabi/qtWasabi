---
type: Spec
id: fidelity/dropdownlist-spec
title: "DropDownList/ComboBox: implementation spec from the reference source"
description: >
  Faithful behavioural specification of Wasabi's DropDownList and
  ComboBox widgets, extracted from the reference source
  (winamp-linux/Src/Wasabi/api/skin/widgets/dropdownlist.cpp,
  combobox.cpp). Basis for replacing the 34-line paint placeholder.
tags: [dropdownlist, combobox, widgets, spec, rb6]
timestamp: 2026-07-04T23:50:00+02:00
related:
  - widgets.md
  - ../roadmap/index.md
---

# DropDownList / ComboBox spec (reference source)

Class chain: `DropDownList : EmbeddedXuiObject` (dropdownlist.h:11),
`ComboBox : DropDownList` (combobox.h:8). XUI tags `Wasabi:DropDownList`
and `Wasabi:ComboBox`; the Maki class is DropDownList only, GUID
`{36D59B71-03FD-4af8-9795-0502B7DB267A}`.

## XML attributes (dropdownlist.cpp:15-23, 75-102)
- `items`: ";"-separated list; setItems clears, adds each, then
  selectDefault (dropdownlist.cpp:518-525).
- `select`: selects by item TEXT, not index (attribute order matters).
- `listheight`: popup height in px, default 128 (ctor :31).
- `maxitems`: 0 = use listheight; -1 = size to all items;
  N = min(N, numItems) * itemHeight plus the list child's y/h offset
  (openList :196-229).
- `feed`, `antialias` pass through.

Look is fully skinnable via system groupdefs, no hardcodes: closed
state = 9-grid `wasabi.objectframe.*` + text child (pad 3/3, width
-23) + 17px arrow button (`wasabi.button.label.arrow.down`, 7x4,
centered). Popup = 9-grid `wasabi.dropdownlist.list.*` around a
`<list nocolheader="1">` with 2px inset over `studio.BaseTexture`.

## Behaviour
- Closed: shows getSelectedText() (or noitemtext when nothing is
  selected). Clicks on text AND button both toggle the popup.
- Open: the list group is a TOP-LEVEL window (not clipped by the
  widget canvas) positioned at the widget's screen rect with
  top=bottom, widget width, height per maxitems formula, scaled by the
  render ratio. Click-outside and Escape close; Space/Return opens on
  focus. Close is DEFERRED (destroy next tick) to avoid UAF from the
  click handler.
- Item click: selectItem(findItem(text), 0) then close.

## Maki API (std.mi:2452-2470; fn table dropdownlist.cpp:539-558)
```
String getItemSelected()          // returns the TEXT (== getSelectedText)
onSelect(Int id, Int hover)      // event
setListHeight(Int h); openList(); closeList()
setItems(String lotsofitems)     // ";"-separated
Int addItem(String text)         // returns id
delItem(Int id); Int findItem(String text)   // -1 when absent
Int getNumItems(); selectItem(Int id, Int hover)
String getItemText(Int id); Int getSelected()   // returns ID, not index
String getSelectedText(); String getCustomText()
deleteAllItems(); setNoItemText(String txt)
```
IDs are NOT indices: a global monotonically growing id_gen across all
instances (dropdownlist.h:36,51). Items are stored case-insensitively
SORTED (wantAutoSort()=1). deleteAllItems sets selected=-1 and does not
reset id_gen.

`onSelect(id, hover)` fires on EVERY selectItem, deliberately without
an early-out on the same id, from: the XML select attribute at load,
Maki selectItem, popup clicks (hover=0). hover=1 is the transient
hover preview (ComboBox hoverselect), which suppresses config writes.

## Usage reality check
Bento does NOT use DropDownList (its only combobox is commented out);
the "Play" dropdown and ML icon menus are gen_ml button MENUS, a
different widget class. Real users: Wasabi:HistoryEditBox
(browser.xml:175) and Winamp freeform dialogs. Implementing this spec
is the foundation for ComboBox/HistoryEditBox and the ML views; the
visible Bento dropdown work item is the Menu popup.

## Mapping to qtWasabi
Replace the placeholder in src/widgets/DropDownList.cpp: closed-state
compositing from the bitmap registry (9-grid frame, text, arrow
button); item state as a sorted QVector{id,text} + global id counter;
popup as a top-level frameless overlay following the detached-window
pattern, deferred destroy via QTimer::singleShot(0); all 17 Maki
functions name-based in wasabi-port/maki-bindings.cpp with onSelect
dispatched through the existing event path. ComboBox later as the
Edit-bearing subclass.
