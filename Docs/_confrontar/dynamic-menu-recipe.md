# Dynamic RE recipe — exact menu bar + keyboard shortcuts

The static `strings` pass gives us menu **commands** but not their **grouping, order, or keyboard
shortcuts** (SwiftUI builds `NSMenu` at runtime; shortcuts are `KeyEquivalent`s assembled in code).
To pin those down exactly, dump the **live** `NSApp.mainMenu` from the running app.

Run this on the machine that can launch Icon Composer (the **macOS 26.4 hackintosh**), then paste the
JSON back — it folds straight into [`editor-ui.md`](editor-ui.md), replacing the "inferred" menu tree.

## Option A — Frida (recommended, clean recursive dump)

```bash
# 1. launch the app, then attach by name
frida "Icon Composer" -l dump_menu.js      # or: frida -n "Icon Composer" -l dump_menu.js
```

`dump_menu.js`:

```js
function flags(mask) {
  const f = [];
  if (mask & 0x100000) f.push('cmd');    // NSEventModifierFlagCommand  1<<20
  if (mask & 0x020000) f.push('shift');  //                            1<<17
  if (mask & 0x080000) f.push('option'); //                            1<<19
  if (mask & 0x040000) f.push('ctrl');   //                            1<<18
  return f;
}
function dumpMenu(menu) {
  const out = [];
  const n = menu.numberOfItems();
  for (let i = 0; i < n; i++) {
    const it = menu.itemAtIndex_(i);
    if (it.isSeparatorItem()) { out.push({ sep: true }); continue; }
    const sub = it.submenu();
    const act = it.action();
    out.push({
      title: it.title().toString(),
      key:   it.keyEquivalent().toString(),           // e.g. "e", "s", ""
      mods:  flags(it.keyEquivalentModifierMask().valueOf()),
      action: act ? act.toString() : null,            // selector (may be SwiftUI-internal)
      enabled: it.isEnabled() ? undefined : false,
      submenu: sub ? dumpMenu(sub) : undefined
    });
  }
  return out;
}
ObjC.schedule(ObjC.mainQueue, function () {
  const app = ObjC.classes.NSApplication.sharedApplication();
  const main = app.mainMenu();
  console.log(JSON.stringify(dumpMenu(main), null, 2));
});
```

> Tip: open a document first (so the document-scoped commands — Layer/Arrange/Export — are populated),
> then run the script. SwiftUI only materializes some menus once a `WindowGroup` scene is active.

## Option B — lldb (no Frida)

```bash
lldb -p "$(pgrep -x 'Icon Composer')"
```
```lldb
expr -l objc -O -- \
  [[[NSApp mainMenu] performSelector:@selector(description)] description]
# for full recursion, load a python helper:
command script import ./dump_menu.py     # walks [NSApp mainMenu] like the JS above
```

## Option C — accessibility snapshot (no injection)

If code injection is undesirable, the AX API exposes titles + `AXMenuItemCmdChar` /
`AXMenuItemCmdModifiers` for the whole menu bar:

```bash
# with the app frontmost:
# use `axcli`/`AXUIElement` walking AXMenuBar → AXMenuBarItem → AXMenu → AXMenuItem
# reading AXTitle, AXMenuItemCmdChar, AXMenuItemCmdModifiers
```

## What to capture

For each menu (File / Edit / View / Layer / Help / app menu):
- exact **title + order** of items and separators,
- **keyEquivalent + modifiers** (⌘, ⇧, ⌥, ⌃),
- the **action selector** where present (maps the item to a command handler we already RE'd:
  `ExportImageCommand`, `ArrangeCommandHandler`, `PasteboardCommandHandler`, `LocalizationMenu`, …).

Paste the JSON back and I'll replace the inferred `menu bar` mermaid in `editor-ui.md` with the
verified tree (titles, shortcuts, and item→command wiring).

## Bonus (same session, cheap)

While attached, two more things worth dumping for the editor docs:
- **Keyboard shortcuts for canvas/tools** — `NSApp.keyWindow` first responder chain / any
  `NSResponder` `keyDown:` maps.
- **Default `SiriGlassView`/specular values** — not needed here, but the same Frida attach can read
  runtime uniform values that are Swift-side (the honest gap noted in `../liquid-glass`).
