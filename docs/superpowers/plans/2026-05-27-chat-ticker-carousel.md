# Chat Ticker Carousel Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a horizontally scrolling news-ticker strip at the top of the Fallout Multi-Chat window that continuously cycles "WELCOME TO FALLOUT 4 GLOBAL CHAT" and "MADE WITH LOVE FROM FALLEN WORLD TEAM".

**Architecture:** All changes are confined to `src/Renderer.cpp`. Static arrays hold the ticker messages and widths. A time-based offset (`ImGui::GetTime() * speed mod totalCycle`) drives text positions computed against a virtual "tape" layout. Text is drawn directly via `ImDrawList::AddText()` with a `PushClipRect`/`PopClipRect` pair so it never bleeds outside the window. A `Dummy()` call advances the ImGui cursor past the strip before `BeginChild`.

**Tech Stack:** C++, ImGui (existing — no new dependencies)

---

### Task 1: Add ticker strip to `Renderer.cpp`

**Files:**
- Modify: `src/Renderer.cpp` — insert ticker block between the `g_chatOpen` header section and the `BeginChild("ScrollingRegion"...)` call (around line 728)

#### Background: how the tape layout works

The ticker imagines a horizontal "tape" scrolling left. Each message is preceded by a blank gap equal to the window width (`winW`), so there is always a full screen-width pause before each message appears:

```
[winW gap][MSG0][winW gap][MSG1]  <- then wraps back
```

- `tapePositions[i]` = left edge of message `i` on the tape
- `totalCycle` = total tape length = sum of `(winW + msgWidth[i])` for all `i`
- `offset = fmodf(time * speed, totalCycle)` — advances each frame
- `screenX = tapePositions[i] - offset` — where message `i` appears on screen (0 = left edge, winW = right edge)
- A second `wrap` pass adds `totalCycle` to `screenX` to catch the message re-entering from the right during the loop transition

- [ ] **Step 1: Add static ticker data** — add these statics near the top of the `Renderer` namespace in `src/Renderer.cpp`, alongside `s_passiveAlpha` and friends (around line 77):

```cpp
static const char* kTickerMessages[] = {
    "WELCOME TO FALLOUT 4 GLOBAL CHAT",
    "MADE WITH LOVE FROM FALLEN WORLD TEAM"
};
static constexpr int   kTickerCount = 2;
static constexpr float kTickerSpeed = 80.0f;
```

- [ ] **Step 2: Insert the ticker strip block** — in `src/Renderer.cpp`, find the line that reads:

```cpp
float footerHeight = g_chatOpen
    ? (ImGui::GetStyle().ItemSpacing.y + ImGui::GetFrameHeightWithSpacing())
    : 0.0f;
ImGui::BeginChild("ScrollingRegion",
```

Insert the following block **immediately before** that `float footerHeight` line:

```cpp
// ── Ticker strip ────────────────────────────────────────────────────────
{
    ImVec2  stripMin = ImGui::GetCursorScreenPos();
    float   winW     = ImGui::GetContentRegionAvail().x;
    float   lineH    = ImGui::GetTextLineHeight();
    float   stripH   = lineH + 4.0f;
    ImVec2  stripMax(stripMin.x + winW, stripMin.y + stripH);

    ImDrawList* dl = ImGui::GetWindowDrawList();
    dl->PushClipRect(stripMin, stripMax, true);

    ImU32 tickerColor = ImGui::ColorConvertFloat4ToU32(
        ImVec4(0.0f, 1.0f, 0.3f, 1.0f));

    float msgWidths[kTickerCount];
    float tapePositions[kTickerCount];
    float totalCycle = 0.0f;
    for (int i = 0; i < kTickerCount; ++i) {
        msgWidths[i]    = ImGui::CalcTextSize(kTickerMessages[i]).x;
        tapePositions[i] = totalCycle + winW;
        totalCycle      += winW + msgWidths[i];
    }

    float offset = fmodf(static_cast<float>(ImGui::GetTime()) * kTickerSpeed, totalCycle);

    for (int i = 0; i < kTickerCount; ++i) {
        for (int wrap = 0; wrap < 2; ++wrap) {
            float screenX = tapePositions[i] - offset
                          + (wrap == 0 ? 0.0f : totalCycle);
            if (screenX < winW && screenX + msgWidths[i] > 0.0f) {
                dl->AddText(
                    ImVec2(stripMin.x + screenX, stripMin.y + 2.0f),
                    tickerColor,
                    kTickerMessages[i]);
            }
        }
    }

    dl->PopClipRect();
    ImGui::Dummy(ImVec2(winW, stripH));
    ImGui::Separator();
}
// ── End ticker strip ─────────────────────────────────────────────────────
```

- [ ] **Step 3: Commit**

```bash
git add src/Renderer.cpp
git commit -m "feat: add scrolling ticker carousel to chat window"
```

---

### Task 2: Build and in-game verification

- [ ] **Step 1: Build**

```powershell
# From E:\F4SE OG\FalloutChat\
cmake --build build
```

Expected: build succeeds with no errors or warnings about the new code.

- [ ] **Step 2: Deploy** — copy the built DLL to `<Fallout4>\Data\F4SE\Plugins\FalloutChat.dll` and launch the game.

- [ ] **Step 3: Verify active mode** — press F11 to open chat. Confirm:
  - A green ticker strip appears above the message history
  - "WELCOME TO FALLOUT 4 GLOBAL CHAT" scrolls in from the right and across
  - After it exits left, "MADE WITH LOVE FROM FALLEN WORLD TEAM" scrolls in
  - The cycle repeats continuously
  - Text does not bleed outside the chat window borders

- [ ] **Step 4: Verify passive mode** — wait for a chat message to arrive while the window is closed. Confirm the ticker is visible in the passive fade-in overlay and scrolls at the same speed.

- [ ] **Step 5: Verify with chat disabled** — uncheck "Enable Chat" in the settings gear popup. Confirm the ticker does not appear (it is gated by `showChat` which requires `g_chatEnabled`).

- [ ] **Step 6: Commit fixups if needed**

```bash
git add src/Renderer.cpp
git commit -m "fix: ticker carousel verification fixups"
```
