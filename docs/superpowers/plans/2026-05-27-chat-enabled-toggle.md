# Chat Enabled Toggle Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a persistent "Enable Chat" checkbox to the in-game settings popup that hides the passive overlay and unread badge while keeping F11 functional.

**Architecture:** A single `bool g_chatEnabled` flag lives in `Renderer.cpp`, is loaded/saved via `main.cpp`'s INI helpers, and is declared `extern` so `main.cpp` can write to it. The checkbox in the existing `##ChatSettings` popup calls `SaveChatEnabled()` on change. The `showChat` condition and unread badge block are both gated on the flag.

**Tech Stack:** C++, SimpleIni, ImGui (existing stack — no new dependencies)

---

### Task 1: Add `g_chatEnabled` flag and INI persistence in `main.cpp`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Declare the extern** — add at the top of `src/main.cpp` after the existing globals (line ~10):

```cpp
bool g_chatEnabled = true;
```

- [ ] **Step 2: Add `SaveChatEnabled()`** — add after `SavePrivacyPolicy()` (after line 58):

```cpp
void SaveChatEnabled(bool enabled)
{
    g_chatEnabled = enabled;
    ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    ini.SetBoolValue("General", "chat_enabled", enabled);
    ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    ini.Reset();
}
```

- [ ] **Step 3: Load the flag in `LoadConfigs()`** — add after the `g_privacyAccepted` line (after line 70):

```cpp
g_chatEnabled = ini.GetBoolValue("General", "chat_enabled", true);
```

- [ ] **Step 4: Commit**

```bash
git add src/main.cpp
git commit -m "feat: add g_chatEnabled flag with INI persistence"
```

---

### Task 2: Wire the flag into `Renderer.cpp` and add the settings checkbox

**Files:**
- Modify: `src/Renderer.cpp`

- [ ] **Step 1: Add extern declarations** — in `Renderer.cpp` near the top where the other externs are declared (around line 21-25), add:

```cpp
extern bool g_chatEnabled;
extern void SaveChatEnabled(bool enabled);
```

- [ ] **Step 2: Add the checkbox to the settings popup** — in `Renderer.cpp` inside the `if (ImGui::BeginPopup("##ChatSettings"))` block, immediately after the `ImGui::Separator();` on the line after "Chat Settings" header (around line 681), add:

```cpp
bool prevEnabled = g_chatEnabled;
ImGui::Checkbox("Enable Chat", &g_chatEnabled);
if (g_chatEnabled != prevEnabled) {
    ::SaveChatEnabled(g_chatEnabled);
    if (!g_chatEnabled && g_chatOpen)
        ToggleChat(true, false);
}
ImGui::Spacing();
```

- [ ] **Step 3: Gate `showChat` on `g_chatEnabled`** — replace the existing `showChat` condition (around line 630):

```cpp
// Before:
bool showChat = (g_chatOpen && ::g_privacyAccepted) ||
                (s_passiveEverTriggered && s_passiveAlpha > 0.01f && !g_chatOpen);

// After:
bool showChat = g_chatEnabled &&
                ((g_chatOpen && ::g_privacyAccepted) ||
                 (s_passiveEverTriggered && s_passiveAlpha > 0.01f && !g_chatOpen));
```

- [ ] **Step 4: Gate the unread badge on `g_chatEnabled`** — find the unread badge block (around line 798):

```cpp
// Before:
if (!g_chatOpen && s_unreadCount > 0) {

// After:
if (g_chatEnabled && !g_chatOpen && s_unreadCount > 0) {
```

- [ ] **Step 5: Commit**

```bash
git add src/Renderer.cpp
git commit -m "feat: chat enable/disable checkbox in settings popup"
```

---

### Task 3: In-game verification

**No automated tests — validate in-game.**

- [ ] Build the DLL:
  ```powershell
  # From E:\F4SE OG\FalloutChat\
  cmake --build build
  ```
  Expected: build succeeds with no errors.

- [ ] Copy the DLL to `<Fallout4>\Data\F4SE\Plugins\FalloutChat.dll` and launch the game.

- [ ] Open chat with F11, open gear settings popup — verify "Enable Chat" checkbox appears at the top, checked by default.

- [ ] Uncheck "Enable Chat" — chat window should close immediately, passive overlay should stop appearing for new messages, unread badge should not show.

- [ ] Press F11 — chat window should open normally despite being disabled.

- [ ] Recheck "Enable Chat" — passive overlay and badge should resume.

- [ ] Quit and relaunch — verify the disabled state is remembered (check `FalloutChat.ini` shows `chat_enabled = false`).

- [ ] **Commit** (if any fixups were needed):
  ```bash
  git add src/Renderer.cpp src/main.cpp
  git commit -m "fix: chat toggle verification fixups"
  ```
