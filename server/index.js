'use strict';

const fs = require('node:fs');
const path = require('node:path');

const PORT           = parseInt(process.env.PORT           || '3000', 10);
const MAX_HISTORY    = parseInt(process.env.MAX_HISTORY    || '50',   10);
const MAX_CLIENTS    = parseInt(process.env.MAX_CLIENTS    || '200',  10);
const MAX_MSG_LEN    = 600;
const MAX_USERNAME_LEN = 32;
const RATE_LIMIT_MS  = 500;
const BOT_SECRET     = process.env.BOT_SECRET || '';
const BANS_FILE      = process.env.BANS_FILE  || path.join(__dirname, 'bans.json');

// Game UserIDs (hex64) exempt from the username filter — admins may use any
// name. Find an id with the bot's !whois command, then list it here.
const ADMIN_USER_IDS = new Set(
  (process.env.ADMIN_USER_IDS || '').split(',').map(s => s.trim()).filter(Boolean)
);

const trustedClients = new WeakSet(); // authenticated bot connections

// Delimiter used only on the trusted bot relay channel: "[REL]<userId>\x02<formatted>"
const REL_DELIM = '\x02';

// ── Ban list ────────────────────────────────────────────────────────────────
// Persisted to BANS_FILE so bans survive restarts. Seed any always-banned IDs
// here; runtime bans (added via the Discord bot) are merged on load.
const seedBans = [
  // 'a1b2c3d4e5f6g7h8',
];

const bannedUserIDs = new Set(seedBans);

function loadBans() {
  try {
    if (fs.existsSync(BANS_FILE)) {
      const raw = JSON.parse(fs.readFileSync(BANS_FILE, 'utf8'));
      if (Array.isArray(raw)) {
        for (const id of raw) if (id) bannedUserIDs.add(String(id));
      }
    }
  } catch (err) {
    console.error(`[bans] failed to load ${BANS_FILE}: ${err.message}`);
  }
  console.log(`[bans] loaded ${bannedUserIDs.size} banned id(s) from ${BANS_FILE}`);
}

function saveBans() {
  try {
    fs.writeFileSync(BANS_FILE, JSON.stringify([...bannedUserIDs], null, 2));
  } catch (err) {
    console.error(`[bans] failed to save ${BANS_FILE}: ${err.message}`);
  }
}

const BAN_NOTICE = 'You are banned from FalloutChat. Contact a moderator if you believe this is a mistake.';

// Deliver an advisory line to one client. Newer clients announce support via
// [HELLO] and receive a styled [SYSTEM] frame; older builds, which understand
// no custom frames, get a plain "Server: text" chat line they can still show.
function sendNotice(ws, text) {
  try {
    if (ws.data && ws.data.supportsSystem) ws.send(`[SYSTEM]${text}`);
    else ws.send(`Server: ${text}`);
  } catch { /* socket already gone */ }
}

// Tell a client why it is being kicked, then close. The close is deferred a
// moment so the notice frame flushes before the close frame.
function notifyBanThenClose(ws) {
  sendNotice(ws, BAN_NOTICE);
  setTimeout(() => { try { ws.close(4000, 'Banned'); } catch { /* already closing */ } }, 300);
}

// Disconnect every connected client matching a freshly-banned id.
function kickBanned(userId) {
  let kicked = 0;
  for (const ws of clients) {
    if (ws.data && ws.data.userId === userId) {
      notifyBanThenClose(ws);
      kicked++;
    }
  }
  return kicked;
}

// ── Blocked usernames ─────────────────────────────────────────────────────
// A display name is rejected if its normalized form CONTAINS any blocked term.
// Normalization folds case, diacritics and common leetspeak so "H1tler",
// "Hîtler" and "h.i.t.l.e.r" all collapse to "hitler". Because names are
// deliberately misspelled (e.g. "Hilter"), seed the obvious variants too and
// extend the list at runtime with !blockname.
const BLOCKED_NAMES_FILE = process.env.BLOCKED_NAMES_FILE || path.join(__dirname, 'blocked_names.json');

const seedBlockedNames = [
  // Extremist / hate
  'hitler', 'hilter', 'adolf', 'adolph', 'nazi', 'naszi', 'fuhrer', 'fuehrer',
  'heil', 'sieg', 'reich', 'thirdreich', 'gestapo', 'wehrmacht', 'aryan',
  'kkk', 'kukluxklan', 'whitepower', 'whitepride', 'bloodandsoil',
  'holocaust', 'holohoax', 'genocide', 'isis', 'jihad', 'taliban', 'alqaeda',
  'fascist', 'goebbels', 'himmler', 'auschwitz', 'swastika', 'lynch',
  // Racial / ethnic slurs
  'nigger', 'nigga', 'niglet', 'negro', 'chink', 'chinky', 'spic', 'kike',
  'gook', 'wetback', 'beaner', 'coon', 'jigaboo', 'porchmonkey', 'paki',
  'raghead', 'towelhead', 'sandnigger', 'gypsy', 'wop', 'dago', 'cracker',
  'honky', 'redskin', 'injun', 'spook', 'tarbaby', 'zipperhead', 'slant',
  // Homophobic / transphobic slurs
  'faggot', 'faggit', 'fagot', 'dyke', 'tranny', 'trannie', 'shemale',
  'homo', 'queer', 'sodomite',
  // Ableist
  'retard', 'retarded', 'spastic', 'spaz', 'mongoloid', 'cripple',
  // Sexual / explicit
  'rape', 'rapist', 'pedo', 'pedophile', 'paedophile', 'molester', 'cunt',
  'whore', 'slut', 'dildo', 'penis', 'vagina', 'pussy',
  'porn', 'incest', 'bestiality', 'jizz', 'fuck', 'shit', 'bitch', 'bastard',
  // Impersonation / authority
  'president', 'admin', 'administrator', 'moderator', 'staff', 'official',
  'server', 'system', 'discord', 'owner', 'developer',
];

const blockedNames = new Set(seedBlockedNames.map(normalizeName).filter(Boolean));

// Collapse a name to comparable letters: lowercase, strip diacritics, fold
// common leet substitutions, then drop everything that isn't a–z.
function normalizeName(s) {
  return String(s)
    .toLowerCase()
    .normalize('NFKD').replace(/[̀-ͯ]/g, '') // strip diacritics
    .replace(/[1!|]/g, 'i')
    .replace(/3/g, 'e')
    .replace(/4/g, 'a').replace(/@/g, 'a')
    .replace(/0/g, 'o')
    .replace(/5/g, 's').replace(/\$/g, 's')
    .replace(/7/g, 't')
    .replace(/[^a-z]/g, '');
}

function matchedBlockedTerm(name) {
  const norm = normalizeName(name);
  if (!norm) return null;
  for (const term of blockedNames) {
    if (term && norm.includes(term)) return term;
  }
  return null;
}

function loadBlockedNames() {
  try {
    if (fs.existsSync(BLOCKED_NAMES_FILE)) {
      const raw = JSON.parse(fs.readFileSync(BLOCKED_NAMES_FILE, 'utf8'));
      if (Array.isArray(raw)) {
        for (const term of raw) {
          const n = normalizeName(term);
          if (n) blockedNames.add(n);
        }
      }
    }
  } catch (err) {
    console.error(`[names] failed to load ${BLOCKED_NAMES_FILE}: ${err.message}`);
  }
  console.log(`[names] ${blockedNames.size} blocked name term(s) from ${BLOCKED_NAMES_FILE}`);
}

function saveBlockedNames() {
  try {
    fs.writeFileSync(BLOCKED_NAMES_FILE, JSON.stringify([...blockedNames], null, 2));
  } catch (err) {
    console.error(`[names] failed to save ${BLOCKED_NAMES_FILE}: ${err.message}`);
  }
}

// ── State ─────────────────────────────────────────────────────────────────
// { ts: 'HH:MM:SS', raw: '<broadcast string>' }
const history = [];
const clients = new Set();

// Strips characters that could confuse the protocol parser on receiving clients
function sanitize(str) {
  return String(str)
    .replace(/[\x00-\x1F\x7F]/g, '')  // control chars (includes \x01 delimiter)
    .trim();
}

// Strips known system prefixes so clients can't spoof them
const SPOOFABLE_PREFIXES = ['[Discord]', '[System]', '[Web]', '[Server]', '[Admin]'];
function stripSpoofedPrefixes(name) {
  let s = name;
  for (const prefix of SPOOFABLE_PREFIXES) {
    while (s.startsWith(prefix)) {
      s = s.slice(prefix.length).trimStart();
    }
  }
  return s || 'Player';
}

function nowTs() {
  return new Date().toTimeString().slice(0, 8);
}

function addHistory(raw) {
  history.push({ ts: nowTs(), raw });
  if (history.length > MAX_HISTORY) history.shift();
}

function broadcastCount() {
  const msg = `[COUNT]:${clients.size}`;
  for (const ws of clients) ws.send(msg);
}

// Broadcast a formatted line to everyone except the sender. Trusted bot
// connections additionally receive the sender's userId so they can moderate.
function broadcastMessage(formatted, senderWs, senderUserId) {
  for (const client of clients) {
    if (client === senderWs) continue;
    if (trustedClients.has(client)) {
      client.send(`[REL]${senderUserId}${REL_DELIM}${formatted}`);
    } else {
      client.send(formatted);
    }
  }
}

// Per-connection rate-limit state stored on the ws object
function isRateLimited(ws) {
  const now = Date.now();
  if (ws.data.lastMsg && (now - ws.data.lastMsg) < RATE_LIMIT_MS) return true;
  ws.data.lastMsg = now;
  return false;
}

// Handle moderation control frames from a trusted bot. Returns true if handled.
function handleControl(ws, msg) {
  if (!trustedClients.has(ws)) return false;

  if (msg.startsWith('[BAN]')) {
    const id = sanitize(msg.slice(5));
    if (!id) { ws.send('[BANERR]empty id'); return true; }
    bannedUserIDs.add(id);
    saveBans();
    const kicked = kickBanned(id);
    console.log(`[ban] added ${id} (kicked ${kicked} active connection(s))`);
    ws.send(`[BANOK]banned ${id} (kicked ${kicked})`);
    return true;
  }

  if (msg.startsWith('[UNBAN]')) {
    const id = sanitize(msg.slice(7));
    const existed = bannedUserIDs.delete(id);
    if (existed) saveBans();
    console.log(`[ban] removed ${id} (was banned: ${existed})`);
    ws.send(existed ? `[BANOK]unbanned ${id}` : `[BANERR]${id} was not banned`);
    return true;
  }

  if (msg.startsWith('[BANLIST]')) {
    ws.send(`[BANLIST]${[...bannedUserIDs].join(',')}`);
    return true;
  }

  if (msg.startsWith('[BLOCKNAME]')) {
    const term = normalizeName(msg.slice(11));
    if (!term) { ws.send('[BANERR]name term is empty after normalization'); return true; }
    blockedNames.add(term);
    saveBlockedNames();
    console.log(`[name] blocked term added: "${term}"`);
    ws.send(`[BANOK]blocked name term "${term}"`);
    return true;
  }

  if (msg.startsWith('[UNBLOCKNAME]')) {
    const term = normalizeName(msg.slice(13));
    const existed = blockedNames.delete(term);
    if (existed) saveBlockedNames();
    ws.send(existed ? `[BANOK]unblocked name term "${term}"` : `[BANERR]"${term}" was not blocked`);
    return true;
  }

  if (msg.startsWith('[BLOCKEDNAMES]')) {
    ws.send(`[BLOCKEDNAMES]${[...blockedNames].join(',')}`);
    return true;
  }

  return false;
}

Bun.serve({
  port: PORT,

  fetch(req, server) {
    if (server.upgrade(req, { data: {} })) return;
    return new Response('FalloutChat WebSocket server', { status: 200 });
  },

  websocket: {
    // Players sit idle between messages; the default 120s idle timeout would
    // silently drop them and make the live count plateau. Keep sockets warm
    // with server-sent pings and a long idle window.
    idleTimeout: 300,   // seconds (Bun max is 960)
    sendPings: true,

    open(ws) {
      if (clients.size >= MAX_CLIENTS) {
        console.warn(`[reject] connection limit reached (${MAX_CLIENTS})`);
        ws.close(1013, 'Server full');
        return;
      }
      clients.add(ws);
      console.log(`[+] connected  total=${clients.size}`);

      for (const h of history) {
        ws.send(`[HISTORY]${h.ts}|${h.raw}`);
      }

      broadcastCount();
    },

    message(ws, data) {
      // 0. Bot authentication — check before length/rate limits
      if (BOT_SECRET && String(data) === `[BOT_AUTH]${BOT_SECRET}`) {
        trustedClients.add(ws);
        console.log('[bot] authenticated');
        return;
      }

      // 0a. Capability handshake — client announces it can render [SYSTEM]
      // frames. Old clients never send this, so they fall back to plain lines.
      if (String(data).startsWith('[HELLO]')) {
        ws.data.supportsSystem = true;
        return;
      }

      // 0b. Moderation control frames (trusted bot only)
      if (handleControl(ws, String(data))) return;

      // 1. Length cap
      if (data.length > MAX_MSG_LEN) {
        console.warn(`[drop] oversized message (${data.length} bytes)`);
        return;
      }

      // 2. Rate limit (trusted bot connections are exempt)
      if (!trustedClients.has(ws) && isRateLimited(ws)) {
        console.warn('[drop] rate limited');
        return;
      }

      const msg = String(data);

      // 3. Rename — silent, no broadcast
      if (msg.startsWith('[RENAME]')) {
        console.log(`[rename] ${msg.slice(8, 80)}`);
        return;
      }

      // 4. Reject raw [EMOTE] from untrusted clients — only server builds these
      if (!trustedClients.has(ws) && msg.startsWith('[EMOTE]')) {
        console.warn('[drop] client tried to inject [EMOTE] directly');
        return;
      }

      // 5. Parse: userID|username[|location]: text
      const ci = msg.indexOf(': ');
      if (ci === -1) {
        console.warn(`[drop] no ': ' delimiter`);
        return;
      }

      const prefix = msg.slice(0, ci);
      const text   = sanitize(msg.slice(ci + 2));
      if (!text || text.length > 500) return;

      const parts   = prefix.split('|');
      const userId = sanitize(parts[0] ?? '');
      const rawName = sanitize(parts[1] ?? 'Player').slice(0, MAX_USERNAME_LEN);

      // Remember which id this connection belongs to (so future bans can kick it)
      ws.data.userId = userId;

      // Check if user is banned by their ID
      if (bannedUserIDs.has(userId)) {
        console.warn(`[ban] banned user ${userId} attempted to send message`);
        notifyBanThenClose(ws);
        return;
      }

      // Trusted bot: keep username as-is (preserves [Discord] prefix)
      // Untrusted:   strip any spoofed system prefixes
      const username = trustedClients.has(ws)
        ? rawName
        : (stripSpoofedPrefixes(rawName) || 'Player');
      const display = username;

      // Reject blocked display names (game clients only; Discord names are
      // moderated on Discord; admin UserIDs are exempt). Drop the message and
      // privately tell the sender, throttled so a misnamed client can't be
      // spammed/spam the log.
      if (!trustedClients.has(ws) && !ADMIN_USER_IDS.has(userId)) {
        const term = matchedBlockedTerm(display);
        if (term) {
          console.warn(`[name] blocked "${display}" (matched "${term}") from ${userId}`);
          const last = ws.data.lastNameNotice || 0;
          if (Date.now() - last > 30000) {
            ws.data.lastNameNotice = Date.now();
            sendNotice(ws, 'Your username is banned and your messages are hidden. Change your name in Settings to chat.');
          }
          return;
        }
      }

      let formatted;
      if (text.startsWith('/me ')) {
        const action = sanitize(text.slice(4));
        if (!action) return;
        formatted = `[EMOTE]${display}\x01${action}`;
        const d = formatted.indexOf('\x01');
        console.log(`[emote] (${userId}) * ${formatted.slice(7, d)} ${formatted.slice(d + 1)}`);
      } else {
        formatted = `${display}: ${text}`;
        console.log(`[chat]  (${userId}) ${display}: ${text}`);
      }

      addHistory(formatted);
      broadcastMessage(formatted, ws, userId);
    },

    close(ws) {
      clients.delete(ws);
      console.log(`[-] disconnected  total=${clients.size}`);
      broadcastCount();
    },

    error(ws, err) {
      console.error(`[ws error] ${err.message}`);
      clients.delete(ws);
    },
  },
});

loadBans();
loadBlockedNames();

// Heartbeat: re-broadcast the count (covers any missed close events) and log
// the true connection count so a plateau can be confirmed server-side.
setInterval(() => {
  let trusted = 0;
  for (const ws of clients) if (trustedClients.has(ws)) trusted++;
  console.log(`[stat] clients=${clients.size} (bots=${trusted}, players=${clients.size - trusted})`);
  broadcastCount();
}, 30000);

console.log(`FalloutChat server  port=${PORT}  max_history=${MAX_HISTORY}  max_clients=${MAX_CLIENTS}`);
