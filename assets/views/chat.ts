// FalloutChat - Main Application Logic (TypeScript)
// Imported AFTER store.ts so window functions are already registered

import { getStore } from './store';

const store = getStore();

// ────────────────────────────────────────────────────────────────────────
// UTILITIES
// ────────────────────────────────────────────────────────────────────────

function escapeHtml(s: string): string {
  return String(s)
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;')
    .replace(/"/g, '&quot;');
}

function nowTs(): string {
  const n = new Date();
  return (
    n.getHours().toString().padStart(2, '0') +
    ':' +
    n.getMinutes().toString().padStart(2, '0') +
    ':' +
    n.getSeconds().toString().padStart(2, '0')
  );
}

function safeCall(fnName: string, ...args: any[]): any {
  try {
    const fn = (window as any)[fnName];
    if (typeof fn === 'function') {
      return fn(...args);
    } else {
      console.warn(`[Chat] Function not available: ${fnName}`);
    }
  } catch (e) {
    console.error(`[Chat] Error calling ${fnName}:`, e);
  }
}

// ────────────────────────────────────────────────────────────────────────
// SCROLL & DRAG
// ────────────────────────────────────────────────────────────────────────

function initScrollHandling(): void {
  const messagesList = document.getElementById('messagesList');
  if (!messagesList) return;

  messagesList.addEventListener(
    'wheel',
    (e: WheelEvent) => {
      e.preventDefault();
      const px =
        e.deltaMode === 1
          ? e.deltaY * 20
          : e.deltaMode === 2
            ? e.deltaY * messagesList.clientHeight
            : e.deltaY;
      messagesList.scrollTop += px;
    },
    { passive: false }
  );
}

function initDragHandling(): void {
  const chatHeader = document.getElementById('chatHeader');
  const chatBox = document.getElementById('chatBox');
  if (!chatHeader || !chatBox) return;

  let dragging = false;
  let dragOffX = 0;
  let dragOffY = 0;

  chatHeader.addEventListener('mousedown', (e: MouseEvent) => {
    if (
      (e.target as HTMLElement).closest('.header-actions') ||
      (e.target as HTMLElement).closest('.settings-overlay')
    ) {
      return;
    }
    dragging = true;
    const r = chatBox.getBoundingClientRect();
    dragOffX = e.clientX - r.left;
    dragOffY = e.clientY - r.top;
    console.log(`[Chat] Drag start (${e.clientX},${e.clientY})`);
    e.preventDefault();
  });

  document.addEventListener('mousemove', (e: MouseEvent) => {
    if (dragging) {
      chatBox.style.left = e.clientX - dragOffX + 'px';
      chatBox.style.top = e.clientY - dragOffY + 'px';
      chatBox.style.bottom = 'auto';
      chatBox.style.right = 'auto';
    }
  });

  document.addEventListener('mouseup', () => {
    if (dragging) {
      const r = chatBox.getBoundingClientRect();
      console.log(`[Chat] Drag end (${Math.round(r.left)},${Math.round(r.top)})`);
    }
    dragging = false;
  });
}

function initResizeHandling(): void {
  const resizeHandle = document.getElementById('resizeHandle');
  const chatBox = document.getElementById('chatBox');
  if (!resizeHandle || !chatBox) return;

  let resizing = false;
  let resizeStartX = 0;
  let resizeStartY = 0;
  let resizeStartW = 0;
  let resizeStartH = 0;

  resizeHandle.addEventListener('mousedown', (e: MouseEvent) => {
    resizing = true;
    const r = chatBox.getBoundingClientRect();
    resizeStartX = e.clientX;
    resizeStartY = e.clientY;
    resizeStartW = r.width;
    resizeStartH = r.height;
    console.log(`[Chat] Resize start ${Math.round(r.width)}x${Math.round(r.height)}`);
    e.preventDefault();
    e.stopPropagation();
  });

  document.addEventListener('mousemove', (e: MouseEvent) => {
    if (resizing) {
      const newW = Math.max(250, resizeStartW + (e.clientX - resizeStartX));
      const newH = Math.max(150, resizeStartH + (e.clientY - resizeStartY));
      chatBox.style.width = newW + 'px';
      chatBox.style.height = newH + 'px';
    }
  });

  document.addEventListener('mouseup', () => {
    if (resizing) {
      const r = chatBox.getBoundingClientRect();
      console.log(`[Chat] Resize end ${Math.round(r.width)}x${Math.round(r.height)}`);
    }
    resizing = false;
  });
}

// ────────────────────────────────────────────────────────────────────────
// SETTINGS MODAL
// ────────────────────────────────────────────────────────────────────────

function toggleSettings(): void {
  const overlay = document.getElementById('settingsOverlay');
  if (!overlay) return;
  overlay.classList.toggle('open');
  console.log(`[Settings] ${overlay.classList.contains('open') ? 'opened' : 'closed'}`);
}

function closeSettings(): void {
  const overlay = document.getElementById('settingsOverlay');
  if (overlay) overlay.classList.remove('open');
  console.log('[Settings] closed');
}

function applyUsername(): void {
  const input = document.getElementById('usernameInput') as HTMLInputElement;
  if (!input) return;

  let name = input.value.trim();
  if (!name) {
    console.warn('[Settings] empty username ignored');
    return;
  }

  name = name.replace(/[\x00-\x1f\x7f]/g, '');
  if (name.length > 32) name = name.substring(0, 32);
  if (!name) {
    console.warn('[Settings] username empty after sanitize');
    return;
  }

  store.updateState({ localUsername: name });
  console.log(`[Settings] setting username "${name}"`);
  safeCall('SendMessage', `/name ${name}`);
  closeSettings();
}

function usernameKeydown(e: KeyboardEvent): void {
  if (e.key === 'Enter') {
    applyUsername();
    e.preventDefault();
  }
  e.stopPropagation();
}

function onFontSize(val: string): void {
  const size = parseInt(val, 10);
  document.documentElement.style.setProperty('--font-size', size + 'px');
  const fontVal = document.getElementById('fontVal');
  if (fontVal) fontVal.textContent = size + 'px';
  console.log(`[Settings] font size ${size}px`);
  store.updateState({ fontSize: size });
  safeCall('SaveFontSize', String(size));
  try {
    localStorage.setItem('fc_font_size', String(size));
  } catch {}
}

function onOpacity(val: string): void {
  const opacity = parseInt(val, 10);
  document.documentElement.style.setProperty('--bg-opacity', (opacity / 100).toFixed(2));
  const opacityVal = document.getElementById('opacityVal');
  if (opacityVal) opacityVal.textContent = opacity + '%';
  console.log(`[Settings] opacity ${opacity}%`);
  store.updateState({ opacity });
  safeCall('SaveOpacity', String(opacity));
  try {
    localStorage.setItem('fc_opacity', String(opacity));
  } catch {}
}

function toggleChatEnabled(): void {
  const state = store.getState();
  const newEnabled = !state.chatEnabled;
  safeCall('setChatEnabled', newEnabled);
  safeCall('SaveChatEnabled', newEnabled ? 'true' : 'false');
}

// Wire up settings buttons to window/global scope for HTML onclick handlers
Object.assign(window, {
  toggleSettings,
  closeSettings,
  applyUsername,
  usernameKeydown,
  onFontSize,
  onOpacity,
  toggleChatEnabled,
});

// ────────────────────────────────────────────────────────────────────────
// ONBOARDING & HUD
// ────────────────────────────────────────────────────────────────────────

let hudDisplayTimer: number | null = null;

function showHudHint(): void {
  const state = store.getState();
  if (!state.chatEnabled || document.body.classList.contains('chat-active')) return;

  console.log('[HUD] showing F11 reminder');
  const hud = document.getElementById('hudReminder');
  if (!hud) return;

  hud.classList.add('visible');
  clearTimeout(hudDisplayTimer ?? undefined);
  hudDisplayTimer = window.setTimeout(() => {
    hud.classList.remove('visible');
    console.log('[HUD] reminder hidden');
  }, 12000);
}

// Setup hint after 3 seconds
setTimeout(showHudHint, 3000);

// ────────────────────────────────────────────────────────────────────────
// PRIVACY & TUTORIAL
// ────────────────────────────────────────────────────────────────────────

function acceptPrivacy(): void {
  console.log('[Privacy] accepted');
  const privacyPolicy = document.getElementById('privacyPolicy');
  if (privacyPolicy) privacyPolicy.classList.remove('active');

  store.updateState({ privacyAccepted: true });
  safeCall('SetPrivacyAccepted');

  const state = store.getState();
  if (!state.tutorialSeen) {
    const tutorialModal = document.getElementById('tutorialModal');
    if (tutorialModal) tutorialModal.classList.add('active');
  } else {
    document.body.classList.remove('modal-active');
    const input = document.getElementById('chatInput') as HTMLInputElement;
    if (input) input.focus();
  }
}

function declinePrivacy(): void {
  console.log('[Privacy] declined');
  safeCall('CloseChat');
}

function acceptTutorial(): void {
  console.log('[Tutorial] accepted');
  const tutorialModal = document.getElementById('tutorialModal');
  if (tutorialModal) tutorialModal.classList.remove('active');

  document.body.classList.remove('modal-active');
  store.updateState({ tutorialSeen: true });
  safeCall('SetTutorialSeen');

  const input = document.getElementById('chatInput') as HTMLInputElement;
  if (input) input.focus();
}

Object.assign(window, { acceptPrivacy, declinePrivacy, acceptTutorial });

// ────────────────────────────────────────────────────────────────────────
// EXTERNAL LINKS
// ────────────────────────────────────────────────────────────────────────

function openDiscord(): void {
  console.log('[Links] opening discord');
  safeCall('OpenURL', 'https://discord.gg/fallenworld');
}

function openWebsite(): void {
  console.log('[Links] opening website');
  safeCall('OpenURL', 'https://fallenworld.nexus/');
}

Object.assign(window, { openDiscord, openWebsite });

// ────────────────────────────────────────────────────────────────────────
// TOAST NOTIFICATIONS
// ────────────────────────────────────────────────────────────────────────

const TOAST_DISPLAY_MS = 8000;
const TOAST_FADE_MS = 600;

function toastDisplayName(sender: string): string {
  return String(sender)
    .replace(/1315627655638290533/g, '[Admin]')
    .replace(/\b\d{15,20}\b\s*/g, '')
    .trim();
}

function isRecentMessage(ts: string): boolean {
  const parts = String(ts).split(':');
  if (parts.length < 2) return true;
  const h = parseInt(parts[0], 10);
  const m = parseInt(parts[1], 10);
  const s = parseInt(parts[2] || '0', 10);
  if (isNaN(h) || isNaN(m) || isNaN(s)) return true;

  const now = new Date();
  const msgSecs = h * 3600 + m * 60 + s;
  const nowSecs = now.getHours() * 3600 + now.getMinutes() * 60 + now.getSeconds();
  let diff = nowSecs - msgSecs;
  if (diff < 0) diff += 86400;
  return diff < 120;
}

function showToast(sender: string, text: string, isEmote: boolean, isMention: boolean): void {
  const state = store.getState();
  if (!state.historyReady) return;
  if (document.body.classList.contains('chat-active')) return;

  store.queueToast(sender, text, isEmote, isMention);
  console.log(`[Toast] queued "${sender}" (queue=${state.toastQueue.length})`);
  if (!state.toastBusy) processToastQueue();
}

function processToastQueue(): void {
  const state = store.getState();
  if (state.toastQueue.length === 0) {
    store.setToastBusy(false);
    return;
  }
  if (document.body.classList.contains('chat-active')) {
    store.clearToastQueue();
    return;
  }

  store.setToastBusy(true);
  const item = store.shiftToastQueue();
  if (!item) return;

  const toast = document.getElementById('toastPopup');
  const senderEl = document.getElementById('toastSender');
  const textEl = document.getElementById('toastText');
  if (!toast || !senderEl || !textEl) return;

  const displayName = toastDisplayName(item.sender);
  toast.classList.remove('hiding', 'mention');
  if (item.isMention) toast.classList.add('mention');
  senderEl.className = 'toast-sender' + (item.isEmote ? ' emote' : '');
  senderEl.textContent = item.isEmote ? `* ${displayName}` : `${displayName}:`;
  textEl.textContent = item.text;
  toast.classList.add('visible');
  console.log(`[Toast] showing "${item.sender}" mention=${item.isMention}`);

  const timers = store.getToastTimers();
  clearTimeout(timers.show ?? undefined);
  clearTimeout(timers.hide ?? undefined);

  const showTimer = window.setTimeout(() => {
    toast.classList.add('hiding');
    const hideTimer = window.setTimeout(() => {
      toast.classList.remove('visible', 'hiding', 'mention');
      processToastQueue();
    }, TOAST_FADE_MS);
    store.setToastTimers(null, hideTimer);
  }, TOAST_DISPLAY_MS);

  store.setToastTimers(showTimer, null);
}

function clearToast(): void {
  const timers = store.getToastTimers();
  clearTimeout(timers.show ?? undefined);
  clearTimeout(timers.hide ?? undefined);
  store.clearToastQueue();

  const toast = document.getElementById('toastPopup');
  if (toast) toast.classList.remove('visible', 'hiding', 'mention');
}

// ────────────────────────────────────────────────────────────────────────
// MESSAGE RENDERING
// ────────────────────────────────────────────────────────────────────────

function formatAdmin(str: string): string {
  let s = String(str);
  const adminHtml = '<span style="color:#ff4444;font-weight:bold;">[Admin]</span>';
  s = s.replace(/&lt;@&amp;1315627655638290533&gt;/g, adminHtml);
  s = s.replace(/&lt;@1315627655638290533&gt;/g, adminHtml);
  return s;
}

function receiveMessage(sender: string, text: string, timestamp: string, isEmote: boolean): void {
  const list = document.getElementById('messagesList');
  if (!list) {
    console.error('[Message] messagesList not found');
    return;
  }

  try {
    const row = document.createElement('div');
    const safeSender = formatAdmin(escapeHtml(sender));
    const safeText = formatAdmin(escapeHtml(text));

    const state = store.getState();
    const isSystem = String(sender).toUpperCase() === 'SYSTEM';
    const isMention = !isSystem && state.localUsername &&
      String(text).toLowerCase().includes(state.localUsername.toLowerCase());

    if (isEmote) {
      row.className = 'msg-row emote' + (isMention ? ' mention' : '');
      row.innerHTML =
        `<span class="msg-time">[${timestamp}]</span> ` +
        `<span class="msg-sender">* ${safeSender}</span> ` +
        `<span class="msg-text">${safeText}</span>`;
    } else if (isSystem) {
      row.className = 'msg-row system';
      row.innerHTML =
        `<span class="msg-time">[${timestamp}]</span> ` +
        `<span class="msg-text">${safeText}</span>`;
    } else {
      row.className = 'msg-row' + (isMention ? ' mention' : '');
      row.innerHTML =
        `<span class="msg-time">[${timestamp}]</span> ` +
        `<span class="msg-sender">${safeSender}:</span> ` +
        `<span class="msg-text">${safeText}</span>`;
    }

    const atBottom = list.scrollHeight - list.scrollTop - list.clientHeight < 40;
    list.appendChild(row);

    while (list.children.length > 200) {
      list.removeChild(list.firstChild!);
    }

    if (atBottom) list.scrollTop = list.scrollHeight;

    if (document.body.classList.contains('passive-mode') && !isSystem && isRecentMessage(timestamp)) {
      showToast(sender, text, isEmote, isMention);
    }
  } catch (err) {
    console.error('[Message] exception:', err);
  }
}

function addSystemMessage(text: string): void {
  console.log('[System] ' + text);
  receiveMessage('System', text, nowTs(), false);
}

Object.assign(window, { receiveMessage, addSystemMessage });

// ────────────────────────────────────────────────────────────────────────
// INPUT HANDLING
// ────────────────────────────────────────────────────────────────────────

function updateCharCount(input: HTMLInputElement): void {
  const count = input.value.length;
  const display = document.getElementById('charCount');
  if (!display) return;

  if (count === 0) {
    display.textContent = '';
  } else {
    display.textContent = `${count}/500`;
    display.classList.toggle('warn', count > 400);
    display.classList.toggle('danger', count > 500);
  }
}

function handleInput(e: KeyboardEvent): void {
  if (e.key !== 'Enter') return;

  const input = document.getElementById('chatInput') as HTMLInputElement;
  if (!input) return;

  const val = input.value.trim();
  if (!val) return;

  if (val === '/help') {
    addSystemMessage('Commands: /name &lt;username&gt; — set display name  |  /me &lt;text&gt; — emote action');
    input.value = '';
    updateCharCount(input);
    e.preventDefault();
    return;
  }

  if (val.length > 500) {
    addSystemMessage('Message too long (max 500 characters).');
    e.preventDefault();
    return;
  }

  console.log(`[Input] sending message (len=${val.length})`);
  safeCall('SendMessage', val);
  input.value = '';
  updateCharCount(input);
  e.preventDefault();
}

Object.assign(window, { updateCharCount, handleInput });

// ────────────────────────────────────────────────────────────────────────
// INITIALIZATION
// ────────────────────────────────────────────────────────────────────────

function init(): void {
  console.log('[Chat] initializing application');
  initScrollHandling();
  initDragHandling();
  initResizeHandling();
  console.log('[Chat] application ready');
}

// Wait for DOM to be ready
if (document.readyState === 'loading') {
  document.addEventListener('DOMContentLoaded', init);
} else {
  init();
}
