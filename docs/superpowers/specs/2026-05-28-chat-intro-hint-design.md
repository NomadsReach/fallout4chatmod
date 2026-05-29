# Chat Intro Hint — Design Spec

**Date:** 2026-05-28  
**Status:** Approved

## Problem

Users who never press F11 have no idea FalloutChat is installed. The mod's UI is entirely hidden until F11 is pressed, so first-time users can install the mod and never discover it exists.

## Goal

Show a small persistent hint widget that tells the user the mod is installed and how to open it. The hint disappears permanently once the user has interacted with the privacy policy (Accept or Decline). It never shows again after that.

## Design

### New Flag: `g_introDismissed`

- Type: `bool`, global in `main.cpp`
- INI key: `intro_dismissed` under `[General]`
- **On load:** initialized to `true` if `privacy_accepted` is already `true`, so existing users who already accepted never see the hint
- **Dismissed when:** user clicks Accept **or** Decline on the privacy policy UI

### New Function: `SaveIntroDismissed()`

Same pattern as `SaveTutorialSeen()`. Sets `g_introDismissed = true`, writes `intro_dismissed = true` to INI.

### Dismiss Trigger

In `Renderer.cpp`, the existing privacy policy buttons:
- "I Agree & Continue" → call `SaveIntroDismissed()` (alongside existing `SavePrivacyPolicy()`)
- "Decline & Close" → call `SaveIntroDismissed()` (alongside existing `ToggleChat`)

### Hint Widget

Rendered every frame when `!g_introDismissed`, outside the `showChat` block (same pattern as unread badge). Uses `ImGui::Begin` with a fixed bottom-right position.

**Position:** `(displayWidth - hintWidth - 16, displayHeight - hintHeight - 16)`  
**Flags:** `NoDecoration | NoMove | NoResize | AlwaysAutoResize | NoSavedSettings | NoFocusOnAppearing | NoNav | NoInputs`  
**Alpha:** 0.92f

**Content:**
```
[green]  FALLOUT CHAT INSTALLED
[dim]    Press F11 to open
```

No pulsing. Static display — it's already the only thing on a blank HUD.

### Unread Badge Conflict

Unread badge: bottom-left (`x=18`). Hint: bottom-right. No overlap.

### Edge Cases

- **Existing user, privacy accepted:** `intro_dismissed` initialized to `privacy_accepted` on load → hint never shown
- **Existing user, previously declined:** hint shows again each load until they interact with privacy policy once more — acceptable, they haven't committed to either path
- **User declines then accepts later:** once dismissed (on first decline), flag stays `true` — hint does not return

## Files Changed

| File | Change |
|------|--------|
| `src/main.cpp` | Add `g_introDismissed`, `SaveIntroDismissed()`, load from INI, initialize from `privacy_accepted` |
| `src/Renderer.cpp` | Extern `g_introDismissed` + `SaveIntroDismissed`, call dismiss on both policy buttons, add hint widget render block |
