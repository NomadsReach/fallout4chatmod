# Chat Intro Hint Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Show a small bottom-right ImGui hint ("FALLOUT CHAT INSTALLED / Press F11 to open") to first-time users, dismissing it permanently once they interact with the privacy policy.

**Architecture:** Add a new `g_introDismissed` bool persisted via INI. The hint renders every frame in the D3D Present hook when the flag is false. Both Accept and Decline buttons in the privacy policy UI set the flag. INI load initializes the flag from `privacy_accepted` so existing users never see it.

**Tech Stack:** C++/F4SE, ImGui, SimpleIni

---

## File Map

| File | Change |
|------|--------|
| `src/main.cpp` | Add `g_introDismissed` global, `SaveIntroDismissed()` function, read `intro_dismissed` in `LoadConfigs()` |
| `src/Renderer.cpp` | Add extern declarations (lines 21-29), call `SaveIntroDismissed()` in both policy buttons, add hint widget render block before `EndFrame()` |

---

### Task 1: Add `g_introDismissed` flag and `SaveIntroDismissed()` to `main.cpp`

**Files:**
- Modify: `src/main.cpp`

- [ ] **Step 1: Add the global and function**

In `src/main.cpp`, after `bool g_tutorialSeen = false;` (line 12), add:

```cpp
bool g_introDismissed = false;
```

After `SaveTutorialSeen()` (after line 79), add:

```cpp
void SaveIntroDismissed()
{
	ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	ini.SetBoolValue("General", "intro_dismissed", true);
	ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
	g_introDismissed = true;
	ini.Reset();
}
```

- [ ] **Step 2: Update `LoadConfigs()` to read the flag**

In `LoadConfigs()`, after the line `g_tutorialSeen = ini.GetBoolValue("General", "tutorial_seen", false);` (line 93), add:

```cpp
bool privacyAlreadyAccepted = ini.GetBoolValue("General", "privacy_accepted", false);
g_introDismissed = ini.GetBoolValue("General", "intro_dismissed", privacyAlreadyAccepted);
```

Note: the default value `privacyAlreadyAccepted` ensures existing users who already accepted the policy never see the hint — their `intro_dismissed` key doesn't exist in INI yet, so it falls back to `true`.

Also remove the now-redundant read of `g_privacyAccepted` on line 91 and replace it with:

```cpp
g_privacyAccepted = privacyAlreadyAccepted;
```

So the `LoadConfigs()` tail looks like:

```cpp
bool privacyAlreadyAccepted = ini.GetBoolValue("General", "privacy_accepted", false);
g_privacyAccepted  = privacyAlreadyAccepted;
g_chatEnabled      = ini.GetBoolValue("General", "chat_enabled", true);
g_tutorialSeen     = ini.GetBoolValue("General", "tutorial_seen", false);
g_introDismissed   = ini.GetBoolValue("General", "intro_dismissed", privacyAlreadyAccepted);
ini.Reset();
```

- [ ] **Step 3: Build and confirm no errors**

```powershell
cd "E:/F4SE OG/Plugins/FalloutChat"
xmake
```

Expected: build succeeds, no errors.

- [ ] **Step 4: Commit**

```powershell
git add src/main.cpp
git commit -m "feat: add g_introDismissed flag and SaveIntroDismissed()"
```

---

### Task 2: Wire up dismiss calls and extern declarations in `Renderer.cpp`

**Files:**
- Modify: `src/Renderer.cpp`

- [ ] **Step 1: Add extern declarations**

In `src/Renderer.cpp`, after line 29 (`extern void SaveTutorialSeen();`), add:

```cpp
extern bool g_introDismissed;
extern void SaveIntroDismissed();
```

- [ ] **Step 2: Call `SaveIntroDismissed()` on Accept**

Find the "I Agree & Continue" button handler (around line 745):

```cpp
if (ImGui::Button("I Agree & Continue", ImVec2(200, 30))) {
    ::SavePrivacyPolicy();
    if (!::g_tutorialSeen)
        s_tutorialStep = 1;
}
```

Replace with:

```cpp
if (ImGui::Button("I Agree & Continue", ImVec2(200, 30))) {
    ::SavePrivacyPolicy();
    ::SaveIntroDismissed();
    if (!::g_tutorialSeen)
        s_tutorialStep = 1;
}
```

- [ ] **Step 3: Call `SaveIntroDismissed()` on Decline**

Find the "Decline & Close" button handler (around line 751):

```cpp
if (ImGui::Button("Decline & Close", ImVec2(200, 30))) {
    ToggleChat(true, false);
}
```

Replace with:

```cpp
if (ImGui::Button("Decline & Close", ImVec2(200, 30))) {
    ::SaveIntroDismissed();
    ToggleChat(true, false);
}
```

- [ ] **Step 4: Build and confirm no errors**

```powershell
cd "E:/F4SE OG/Plugins/FalloutChat"
xmake
```

Expected: build succeeds, no errors.

- [ ] **Step 5: Commit**

```powershell
git add src/Renderer.cpp
git commit -m "feat: dismiss intro hint on privacy policy accept or decline"
```

---

### Task 3: Add the hint widget

**Files:**
- Modify: `src/Renderer.cpp`

- [ ] **Step 1: Add the hint render block**

In `src/Renderer.cpp`, find the unread badge block ending (around line 1030):

```cpp
				}

				ImGui::EndFrame();
```

Insert the hint block between the closing `}` of the badge block and `ImGui::EndFrame()`:

```cpp
				// ── Intro hint ───────────────────────────────────────────────────────
				if (!::g_introDismissed) {
					ImGuiIO& hintIO = ImGui::GetIO();
					constexpr float padX = 12.0f, padY = 8.0f;
					ImVec2 hintSize(220.0f, 52.0f);
					ImVec2 hintPos(
						hintIO.DisplaySize.x - hintSize.x - 16.0f,
						hintIO.DisplaySize.y - hintSize.y - 16.0f
					);
					ImGui::SetNextWindowPos(hintPos, ImGuiCond_Always);
					ImGui::SetNextWindowSize(hintSize, ImGuiCond_Always);
					ImGui::SetNextWindowBgAlpha(0.92f);
					ImGui::Begin("##IntroHint", nullptr,
						ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
						ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoSavedSettings |
						ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav |
						ImGuiWindowFlags_NoInputs);
					ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.6f, 1.0f), "FALLOUT CHAT INSTALLED");
					ImGui::TextDisabled("Press F11 to open");
					ImGui::End();
				}

				ImGui::EndFrame();
```

- [ ] **Step 2: Build and confirm no errors**

```powershell
cd "E:/F4SE OG/Plugins/FalloutChat"
xmake
```

Expected: build succeeds, no errors.

- [ ] **Step 3: Commit**

```powershell
git add src/Renderer.cpp
git commit -m "feat: show intro hint until privacy policy is accepted or declined"
```

---

## Validation (in-game)

- Fresh install (no INI): hint visible bottom-right on first load. After pressing F11 and accepting/declining privacy policy, hint disappears. Reload game — hint does not return.
- Existing user with `privacy_accepted = true` in INI: hint never appears.
- Existing user with `privacy_accepted = false` (previously declined): hint appears, dismissed on next privacy policy interaction.
