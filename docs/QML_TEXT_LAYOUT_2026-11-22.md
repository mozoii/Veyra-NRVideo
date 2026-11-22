# QML text layout pass (2026-11-22)

English source strings are about twice the width of the Chinese ones the layouts were
measured against, so a number of fixed widths and slots cut or overlapped text. This
note records what was measured, what changed, and what is still open. It is a layout
record, not a release note.

## How it was measured

A private in-process audit (`build-local/verify/`, untracked) appends a snippet to the
staged `Main.qml`, walks the live scene graph and reports, per page and dialog:

- `elided` — `Text.truncated` is true (the string is visibly cut);
- `wide` / `tall` — `contentWidth` / `contentHeight` exceed the item's own box;
- `offwindow` — the visible rect leaves the window (clipped-away scrolled content is
  excluded by intersecting every clipping ancestor);
- `overlap` — two visible text rects intersect by more than 2px in both axes, excluding
  ancestor/descendant pairs and content clipped away by a Flickable;
- `summary` — one line per audit with the text count and issue count.

Rectangles are mapped by both corners, because the node canvas scales its world with a
zoom transform; a width added to one mapped corner produced false 2-3px overlaps until
that was fixed. Pages are addressed by `pageId`, because every page is a `VPage`.

The tour covers: home; the Pro page on all five inspector tabs in list mode; the node
page with six real chain nodes; the Export page; all ten Settings sections; and the nine
dialogs (capture, moonlight, xbox, screen, subtitle, audio, save, manage, translation).
Each page is audited twice, because cards and pages animate into place and the first
sample is a transient.

Commands (Windows, `VEYRA_LOG_FILE` + private `--data-dir`, `--exit-after`):

```
python build-local/verify/tour.py | xargs -0 -n1 echo        # print the plan
python build-local/verify/audit.py build-local/x64-release build-local/verify/final-1280x800 "<plan-json>" --size 1280x800
```

Final result, English source, zero findings in every audit:

| size | audits clean | elided/wide/overlap/offwindow |
| --- | --- | --- |
| 1280x800 | 58/58 | 0 |
| 1024x700 | 58/58 | 0 |
| 1920x1080 | 58/58 | 0 |
| 800x600 | 56/58 | 9 (see below) |

## What was fixed

- `VSlider.qml` — the reset glyph (`↺`) was a 16px symbol with a 22px line box inside a
  20px `ToolButton`, so its top and bottom were cut in every row that shows a slider.
  Now 14px, which fits the control.
- `ProPage.qml` — the display-state value "Unavailable now · 240 Hz" was elided at
  115px. The variable-refresh value now wraps to two lines
  (`Text.WrapAtWordBoundaryOrAnywhere`, so a single long translation word breaks instead
  of overflowing). The Input / Output / Optical flow value cells gained `elide`, which
  they were missing: a longer translation was clipped, not shortened.
- `HomePage.qml` — the source-card subtitle ("Account sign-in · experimental") was cut
  at 136px. It now wraps to two lines in the 124px card and stays single-line in the
  80px compact card.
- `NodePage.qml`
  - Node cards: the width was a per-type constant (272 / 320 / 360) measured for Chinese
    names, which cut "NR enhancement · layer 1" and "DLSS Frame Generation". The card
    width is now `max(base, header implicit width + 12)`, capped at 460.
  - The cost badge is capped at 118px with `elide` and always carries a tooltip, so a
    long status ("SDR display, not converted") cannot push the node name out.
  - The timing strip: six waiting segments of 72px each cut every label to
    "NR enhancemen…". Waiting segments now show the node name (the state word moved to
    the tooltip, which already had the full detail) and take the width their own name
    needs, giving space back to the timed bars when the strip is narrow.
  - `fitView` reserved 90px at the bottom; the floating tools bar and timing strip
    occupy 84, so the tallest card's last row slid under them. Reserved properly.
    `settle` now calls `fitView` when the boxes no longer fit the canvas, which is what
    keeps a wider English chain on screen instead of under the overlays.
- `SavePresetDialog.qml` — the node-name column was a fixed 110px and cut "NR protected
  areas" and "DLSS Frame Generation"; it is now measured with `FontMetrics` over the
  chain's names (capped at 200).
- `tests/qml/QuickSmokeTests.cpp` — a duplicated `qmlRegisterType(` line left the QML
  test target uncompilable in this fork (the PS5 removal deleted one line too many).
  Restored, so `veyra_qml_quick_tests` builds and runs.

## Validation actually run

- Layout audit: 58/58 clean at 1280x800 and 1024x700, 58/58 at 1920x1080, German and
  Russian tours have **no** overlaps or off-window findings (elisions remain, see below).
- `python -B scripts/i18n/extract.py --check`: `entries=2996 dropped=[] missing=0 (all
  nine languages) placeholder problems=0`. No new source strings were added.
- `qmllint` on the five changed files: no errors (pre-existing warnings only).
- `veyra_qml_quick_tests`: 229 passes, 2 failures, both
  `tst_beauty_list_delete.qml:99 test_direct_delete_can_remove_last_layer`
  (enabled/disabled). Verified **pre-existing**: with the five QML files stashed and the
  staged QML recopied, the same two failures reproduce (`build-local/x64-release/before.xml`).

## Still open

- **800x600 node canvas.** Two cards still overlap (by about 76x12 world px at 0.25 zoom)
  and card labels sit under the floating tools bar. A 6-node chain does not fit an
  800x600 window whose video card takes 330 of the 600px. No text is cut; the canvas
  runs out of room. `settle` cannot separate boxes past the zoom floor.
- **Long translations elide, by design.** German and Russian leave elided labels in the
  Pro display card (value column shrinks to ~61px), the Settings section nav
  ("Komponenten und Lizenzen"), and the node preset button
  ("Knoten-Preset: Benutzerdefiniert"). All three carry tooltips. The structural fix is
  a stacked label/value layout in the narrow status card, which is a design change and
  was not made here.
- **Dialogs that need live state were only partly exercised.** `dlg-xbox` renders no text
  without an Xbox session, and the capture dialog shows its no-source state. Neither was
  audited with a device or console attached.
- **No physical display check.** The audit reads the scene graph, not pixels; it cannot
  see sub-pixel clipping, glyph fallback or a clipped background behind text.
