// Global type declarations for F4SE-injected functions
// These are called from C++ and made available to JavaScript

declare global {
  interface Window {
    // ── F4SE Plugin Communication ──────────────────────────────────────
    // Sent from JavaScript to F4SE (registered listeners)
    SendMessage: (text: string) => void;
    SaveFontSize: (size: string) => void;
    SaveOpacity: (opacity: string) => void;
    SaveChatEnabled: (enabled: string) => void;
    SetPrivacyAccepted: () => void;
    SetTutorialSeen: () => void;
    CloseChat: () => void;
    OpenURL: (url: string) => void;

    // ── Lifecycle and State Management ────────────────────────────────
    // Called from C++ to initialize state and settings
    setupOnboarding: (intro: boolean, priv: boolean, tut: boolean) => void;
    onChatOpened: () => void;
    onChatClosed: () => void;
    setChatEnabled: (enabled: boolean) => void;
    markHistoryDone: () => void;
    SetUsernameInput: (name: string) => void;

    // ── Message Reception ─────────────────────────────────────────────
    // Called from C++ when new messages arrive or history is replayed
    receiveMessage: (sender: string, text: string, timestamp: string, isEmote: boolean) => void;
    addSystemMessage: (text: string) => void;

    // ── Internal Store Access ─────────────────────────────────────────
    // For module-internal use (not called from C++)
    __chatStore?: any;

    // ── Standard DOM Globals (for type checking) ──────────────────────
    localStorage: Storage;
  }

  // Global function stubs for safe calls
  namespace Window {
    var SendMessage: (text: string) => void;
    var SaveFontSize: (size: string) => void;
    var SaveOpacity: (opacity: string) => void;
    var SaveChatEnabled: (enabled: string) => void;
  }
}
