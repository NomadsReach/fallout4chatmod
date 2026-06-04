# UserID System Integration Guide

## Changes to main.cpp

### 1. Include the header
Add at the top with other includes:
```cpp
#include "UserID.h"
```

### 2. Remove/replace global Steam ID tracking
**Remove:**
```cpp
std::atomic<uint64_t> g_steamID = 0;
uint64_t FetchSteamID() { ... }  // Delete entire function
```

**Keep (but update):**
```cpp
std::string username = "Player";  // Keep, but will be overridden by UserID
```

### 3. Update F4SEPlugin_Load (kGameDataReady handler)
**Before:** Calls `FetchSteamID()` and stores in g_steamID

**After:** Initialize UserID system
```cpp
if (msg->type == F4SE::MessagingInterface::kGameDataReady) {
    // Initialize persistent user ID
    FalloutChat::UserID::GetSingleton().Initialize();
    
    auto& userID = FalloutChat::UserID::GetSingleton();
    std::string userIDValue = userID.GetID();
    username = userID.GetUsername();  // Load saved username
    
    logger::info("FalloutChat: User ID: {} (username: {})", userIDValue, username);

    auto* api = PRISMA_UI_API::RequestPluginAPI<PRISMA_UI_API::IVPrismaUI3>();
    if (!api) {
        logger::error("[FalloutChat] Failed to get PrismaUI API V3");
        return;
    }

    FalloutChat::ChatUI::GetSingleton().Initialize(api);
    FalloutChat::ChatClient::GetSingleton().Initialize(serverUrl, username, userIDValue);

    // Register for menu events, key handler, etc...
    // ... rest of kGameDataReady code ...
}
```

### 4. Update SaveUsername function
**Before:**
```cpp
void SaveUsername(const std::string& newName)
{
    username = newName;
    std::lock_guard<std::mutex> lock(g_iniMutex);
    ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    ini.SetValue("General", "username", newName.c_str());
    ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    // ... notify ChatClient ...
}
```

**After:**
```cpp
void SaveUsername(const std::string& newName)
{
    username = newName;
    FalloutChat::UserID::GetSingleton().SetUsername(newName);  // Saves to AppData
    
    // Also update INI for backwards compatibility if desired
    std::lock_guard<std::mutex> lock(g_iniMutex);
    ini.LoadFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    ini.SetValue("General", "username", newName.c_str());
    ini.SaveFile("Data\\F4SE\\Plugins\\FalloutChat.ini");
    // ... notify ChatClient ...
}
```

---

## Changes to ChatUI.cpp / ChatClient.cpp

### Remove Steam fallback logic
**Remove this block from ChatUI.cpp (lines ~84-91):**
```cpp
if (g_steamID == 0) {
    logger::info("ChatUI: SteamID not cached, attempting fetch");
    g_steamID = FetchSteamID();
    if (g_steamID != 0) {
        logger::info("ChatUI: SteamID fetched late: {}", g_steamID.load());
        ChatClient::GetSingleton().SetSteamID(g_steamID);
    }
}
```

No longer needed — UserID is persistent and always available.

### Update SendMessage validation
**Before:**
```cpp
if (g_steamID == 0) {
    logger::warn("ChatUI: SendMessage blocked — SteamID unavailable");
    g_api->Invoke(g_view, "addSystemMessage('Unable to verify Steam status. Try again in a moment.')");
    return;
}
```

**After:**
```cpp
// UserID is always available (generated on first launch, persisted)
// No validation needed — always safe to send
```

---

## Changes to ChatClient

### Update Initialize signature
**Before:**
```cpp
void Initialize(const std::string& url, const std::string& user, uint64_t steamID);
```

**After:**
```cpp
void Initialize(const std::string& url, const std::string& user, const std::string& userID);
```

Update member:
```cpp
std::string _userID;  // Changed from uint64_t to string
```

### Update message sending
**Before:**
```cpp
std::string msg = std::to_string(_steamID) + "|" + _username + ": " + text;
```

**After:**
```cpp
std::string msg = _userID + "|" + _username + ": " + text;
```

---

## Server-Side Changes

Update `server/index.js` to accept string user ID instead of SteamID:

```javascript
// Parse message format: "userID|username: text"
const parts = data.split('|', 2);
if (parts.length < 2) return;

const userID = parts[0];           // Now a hex string, not decimal
const rest = parts[1];

// Check ban list by userID (not SteamID)
if (bannedIDs.includes(userID)) {
    console.log(`[BAN] User ${userID} attempted to connect`);
    ws.close(4000, "Banned");
    return;
}

// Rest of message parsing...
```

---

## Testing Checklist

- [ ] UserID file created at `%APPDATA%\Local\FalloutChat\user_id.json` on first launch
- [ ] File contains: `id`, `username`, `created` timestamp
- [ ] Username can be changed in-game and persists across restarts
- [ ] Deleting the ID file and relaunching generates a new ID
- [ ] Chat messages sent with correct format: `hex64_id|username: text`
- [ ] Server receives and displays messages correctly
- [ ] Ban list blocks by hex64 ID (server-side verification)

---

## File Locations

**Client-side ID file:**
```
%APPDATA%\Local\FalloutChat\user_id.json
```

**Server ban list (example):**
```javascript
// server/index.js
const bannedIDs = [
    "a1b2c3d4e5f6g7h8",
    "x9y8z7w6v5u4t3s2",
];
```

You can load this from a database or JSON file depending on your backend.
