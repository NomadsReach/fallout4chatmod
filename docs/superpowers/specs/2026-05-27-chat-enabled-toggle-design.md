# Chat Enabled Toggle — Design Spec

**Date:** 2026-05-27

## Summary

Add a persistent "Enable Chat" checkbox to the in-game settings popup that suppresses the passive chat overlay and unread badge. F11 always opens the full interactive chat window regardless of this setting.

## Flag & Storage

- New `bool g_chatEnabled = true` in `Renderer.cpp` (file-scope static, alongside `s_passiveMaxAlpha` etc.).
- Exposed as `extern bool g_chatEnabled` in `main.cpp`.
- INI key: `[General] chat_enabled` (boolean). Default: `true`.

## INI Functions (main.cpp)

- `LoadConfigs()` reads `chat_enabled` via `ini.GetBoolValue("General", "chat_enabled", true)` and stores it in `g_chatEnabled`.
- New `SaveChatEnabled(bool enabled)` writes the value and saves the file — same pattern as `SavePrivacyPolicy()`.

## Settings Popup (Renderer.cpp — ##ChatSettings)

At the top of the popup, before the opacity slider:

```
[x] Enable Chat
```

- `ImGui::Checkbox("Enable Chat", &g_chatEnabled)`
- On change: call `::SaveChatEnabled(g_chatEnabled)`.
- If disabling while chat is open: also call `ToggleChat(true, false)` to close it.

## Behavior When Disabled

| Feature | Disabled behaviour |
|---|---|
| Passive fade-in overlay | Hidden |
| Unread badge | Hidden |
| F11 open/close | Works normally |
| Full interactive chat window | Opens/closes normally via F11 |
| WebSocket connection | Unaffected (stays connected) |

The `showChat` condition becomes:
```cpp
bool showChat = g_chatEnabled &&
    ((g_chatOpen && ::g_privacyAccepted) ||
     (s_passiveEverTriggered && s_passiveAlpha > 0.01f && !g_chatOpen));
```

The unread badge block is gated: `if (g_chatEnabled && !g_chatOpen && s_unreadCount > 0)`.

## No Changes To

- WebSocket / ChatClient
- F11 / ESC key handling
- Privacy policy flow
