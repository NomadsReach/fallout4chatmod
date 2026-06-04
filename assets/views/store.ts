// FalloutChat State Store
// All state lives here, window functions registered at module load time

interface ChatMessage {
  sender: string;
  text: string;
  timestamp: string;
  isEmote: boolean;
  isSystem?: boolean;
  isMention?: boolean;
}

interface UIState {
  chatEnabled: boolean;
  introDismissed: boolean;
  privacyAccepted: boolean;
  tutorialSeen: boolean;
  chatActive: boolean;
  settingsOpen: boolean;
  localUsername: string;
  fontSize: number;
  opacity: number;
  toastQueue: Array<{ sender: string; text: string; isEmote: boolean; isMention: boolean }>;
  toastBusy: boolean;
  historyReady: boolean;
  messageCount: number;
}

class ChatStore {
  private state: UIState;
  private messages: ChatMessage[] = [];
  private toastShowTimer: number | null = null;
  private toastHideTimer: number | null = null;

  constructor() {
    this.state = {
      chatEnabled: true,
      introDismissed: false,
      privacyAccepted: false,
      tutorialSeen: false,
      chatActive: false,
      settingsOpen: false,
      localUsername: '',
      fontSize: 14,
      opacity: 60,
      toastQueue: [],
      toastBusy: false,
      historyReady: false,
      messageCount: 0,
    };
  }

  getState(): Readonly<UIState> {
    return { ...this.state };
  }

  updateState(partial: Partial<UIState>): void {
    this.state = { ...this.state, ...partial };
  }

  addMessage(msg: ChatMessage): void {
    this.messages.push(msg);
    this.state.messageCount++;
    if (this.messages.length > 200) {
      this.messages.shift();
    }
  }

  getMessages(): ChatMessage[] {
    return [...this.messages];
  }

  clearMessages(): void {
    this.messages = [];
  }

  queueToast(sender: string, text: string, isEmote: boolean, isMention: boolean): void {
    this.state.toastQueue.push({ sender, text, isEmote, isMention });
  }

  shiftToastQueue(): { sender: string; text: string; isEmote: boolean; isMention: boolean } | undefined {
    return this.state.toastQueue.shift();
  }

  setToastBusy(busy: boolean): void {
    this.state.toastBusy = busy;
  }

  setToastTimers(show: number | null, hide: number | null): void {
    this.toastShowTimer = show;
    this.toastHideTimer = hide;
  }

  getToastTimers(): { show: number | null; hide: number | null } {
    return { show: this.toastShowTimer, hide: this.toastHideTimer };
  }

  clearToastQueue(): void {
    this.state.toastQueue = [];
    this.state.toastBusy = false;
  }
}

// Global singleton
const store = new ChatStore();

// ────────────────────────────────────────────────────────────────────────
// WINDOW FUNCTION REGISTRATIONS — These fire BEFORE the framework initializes
// ────────────────────────────────────────────────────────────────────────

// Setup onboarding state
window.setupOnboarding = function (intro: boolean, priv: boolean, tut: boolean): void {
  store.updateState({
    introDismissed: intro,
    privacyAccepted: priv,
    tutorialSeen: tut,
  });
  console.log(`[Store] Onboarding: intro=${intro} priv=${priv} tut=${tut}`);
};

// Set username input field (from settings)
window.SetUsernameInput = function (name: string): void {
  const input = document.getElementById('usernameInput') as HTMLInputElement;
  if (input) {
    input.value = name;
    store.updateState({ localUsername: name });
  }
};

// Chat opened event
window.onChatOpened = function (): void {
  store.updateState({ chatActive: true });
  document.body.classList.remove('chat-startup', 'passive-mode');
  document.body.classList.add('chat-active');
  console.log('[Store] Chat opened');
};

// Chat closed event
window.onChatClosed = function (): void {
  store.updateState({ chatActive: false, settingsOpen: false });
  document.body.classList.remove('chat-active', 'modal-active');
  document.body.classList.add('passive-mode');
  const input = document.getElementById('chatInput') as HTMLInputElement;
  if (input) input.value = '';
  console.log('[Store] Chat closed');
};

// Set chat enabled state
window.setChatEnabled = function (enabled: boolean): void {
  store.updateState({ chatEnabled: enabled });
  const toggle = document.getElementById('chatEnabledToggle');
  if (toggle) toggle.classList.toggle('on', enabled);
  document.body.classList.toggle('chat-disabled', !enabled);
  console.log(`[Store] Chat enabled: ${enabled}`);
};

// Mark history as loaded (enables live toasts)
window.markHistoryDone = function (): void {
  store.updateState({ historyReady: true });
  console.log('[Store] History loaded, live toasts enabled');
};

// Export for module imports
(window as any).__chatStore = store;

export function getStore(): ChatStore {
  return store;
}

console.log('[FalloutChat] Store initialized and window functions registered');
