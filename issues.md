# FalloutChat — Known Issues

---

## Open

### 1. Double cursor when chat is open
**Status:** In progress  
**Root cause:** When `g_api->Focus()` is called, PrismaUI sets a Windows OS cursor via its `OnChangeCursor` implementation in Ultralight. This OS cursor renders on top of Fallout 4's own sprite cursor, producing a double cursor. CSS `cursor: none` and data-URI transparent GIF both failed because PrismaUI may call `SetCursor()` after the CSS is evaluated.  
**Fix:** Call `ShowCursor(FALSE)` from C++ immediately after `g_api->Focus()` in `ToggleChat()`, and `ShowCursor(TRUE)` on close. Keep `cursor: none !important` in CSS as belt-and-suspenders.

---

### 2. Settings panel is missing the full feature set from the old ImGui UI
**Status:** In progress  
**Features that existed in the old `##ChatSettings` ImGui popup that must be in the HTML settings panel:**
- Enable Chat toggle (saved to `FalloutChat.ini` → `chat_enabled`)
- Font size slider (range 10–20px, saved to `FalloutChat.ini` → `font_size`)
- Background opacity slider (range 0.1–1.0, saved to `FalloutChat.ini` → `bg_opacity`)
- Commands reference list: `/name <username>`, `/me <action>`, `F11` toggle, `ESC` close
- Replay Tutorial button (currently no-op until tutorial is ported)

---

### 3. Website icon missing from header
**Status:** In progress  
**Root cause:** Accidentally removed the globe/website icon button when rewriting the HTML. Only Discord button remains.  
**Fix:** Add the globe SVG button back next to the Discord button.

---

### 4. History only shows server's last 20 messages
**Status:** By design (server limitation) — not a client bug  
**Detail:** Confirmed in log. The server sends exactly 20 `[HISTORY]` frames on connect. If those 20 messages are hours old it means no one has chatted recently. There is nothing the client can do; the history window is controlled server-side.  
**Evidence:** Log shows 20 history messages (02:22–14:38) and zero live messages after — no one was chatting during the test session.

---

## Resolved

- **Deadlock in WebSocket callback** — `ShutdownNoLock()` extracted; lock released before `AddTask` dispatch.
- **Cross-thread `Invoke` crash** — all `Invoke` calls dispatched via `F4SE::GetTaskInterface()->AddTask`.
- **History not loading on join** — queue flushed in `OnDomReady` via `AddTask`.
- **`addSystemMessage` undefined** — caused silent JS crash that blocked all subsequent `receiveMessage` calls. Defined in HTML.
- **Black screen on F11** — `Focus(view, true, false)` had `pauseGame=true`. Fixed to `false`.
- **Key sink registered too late** — `KeyHandler::RegisterSink()` moved to `Initialize()` (kGameDataReady) from `CreateView()`.
- **JS injection via sender/text/username** — `EscapeJS()` helper applied to all `Invoke` string parameters.
- **`SetPrivacyAccepted` not saving intro dismissed** — `SaveIntroDismissed()` added to listener.
- **Re-init deadlock** — `Initialize()` called `Shutdown()` while holding mutex; fixed with `ShutdownNoLock()`.
- **Drag broken** — CSS `resize: both` not supported in Ultralight; replaced with JS drag handler on header.
- **Resize broken** — Replaced with JS resize handle in bottom-right corner.
- **Cogwheel/settings gone** — Added back with settings panel.
- **Emote styling missing** — `isEmote` flag now passed through C++ → JS → rendered in italic purple.
