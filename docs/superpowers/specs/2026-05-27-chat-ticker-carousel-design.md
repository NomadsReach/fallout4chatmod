# Chat Ticker Carousel — Design Spec

**Date:** 2026-05-27

## Summary

Add a horizontally scrolling news-ticker strip at the top of the Fallout Multi-Chat window that cycles through two messages continuously. Visible whenever the chat window is shown (active or passive mode).

## Messages

Looping sequence:
1. `WELCOME TO FALLOUT 4 GLOBAL CHAT`
2. `MADE WITH LOVE FROM FALLEN WORLD TEAM`

## Layout

A fixed-height strip (~18px) rendered at the top of the chat window body, between the header controls and the message history `BeginChild`. Text scrolls right-to-left continuously. A clip rect constrains the text to the strip width so it never bleeds outside the window.

## Animation

- **Speed:** 80px/sec (readable at a glance without feeling slow)
- **Drive:** `ImGui::GetTime() * speed` gives the running offset
- **Sequencing:** Each message starts from the right edge (`windowWidth`) and scrolls to `-textWidth`. When a message exits the left edge, the next message starts from the right. Messages do not overlap.
- **Loop:** After the last message, the sequence wraps back to the first.

## Rendering

Use `ImDrawList::AddText()` on `ImGui::GetWindowDrawList()` with `PushClipRect` / `PopClipRect` to contain the text. No `ImGui::Text()` call — direct draw list rendering is required so the clip rect works correctly and the text doesn't consume layout height twice.

The strip occupies exactly one `ImGui::GetTextLineHeight() + 4px` of vertical space via a dummy `ImGui::Dummy()` call to advance the cursor.

## Style

- Color: `ImVec4(0.0f, 1.0f, 0.3f, 1.0f)` (Fallout green, matching sender palette)
- Font: default ImGui font (same as chat text)
- Background: none (transparent, inherits window background)

## Visibility

Gated by the existing `showChat` boolean — the ticker appears and disappears with the chat window in both active and passive modes. No separate visibility logic needed.

## No Changes To

- Message history scroll region
- Input bar
- Settings popup
- `g_chatEnabled` toggle (ticker respects it automatically via `showChat`)
