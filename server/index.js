'use strict';

const PORT           = parseInt(process.env.PORT           || '3000', 10);
const MAX_HISTORY    = parseInt(process.env.MAX_HISTORY    || '50',   10);
const MAX_CLIENTS    = parseInt(process.env.MAX_CLIENTS    || '200',  10);
const MAX_MSG_LEN    = 600;
const MAX_USERNAME_LEN = 32;
const RATE_LIMIT_MS  = 500;
const BOT_SECRET     = process.env.BOT_SECRET || '';

const trustedClients = new WeakSet(); // authenticated bot connections

// Banned user IDs (hex64 format: "a1b2c3d4e5f6g7h8")
const bannedUserIDs = [
  // Add banned user IDs here
];

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

// Per-connection rate-limit state stored on the ws object
function isRateLimited(ws) {
  const now = Date.now();
  if (ws.data.lastMsg && (now - ws.data.lastMsg) < RATE_LIMIT_MS) return true;
  ws.data.lastMsg = now;
  return false;
}

Bun.serve({
  port: PORT,

  fetch(req, server) {
    if (server.upgrade(req, { data: {} })) return;
    return new Response('FalloutChat WebSocket server', { status: 200 });
  },

  websocket: {
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

      // Check if user is banned by their ID
      if (bannedUserIDs.includes(userId)) {
        console.warn(`[ban] banned user ${userId} attempted to send message`);
        ws.close(4000, 'Banned');
        return;
      }

      // Trusted bot: keep username as-is (preserves [Discord] prefix)
      // Untrusted:   strip any spoofed system prefixes
      const username = trustedClients.has(ws)
        ? rawName
        : (stripSpoofedPrefixes(rawName) || 'Player');
      const display = username;

      let formatted;
      if (text.startsWith('/me ')) {
        const action = sanitize(text.slice(4));
        if (!action) return;
        formatted = `[EMOTE]${display}\x01${action}`;
        const d = formatted.indexOf('\x01');
        console.log(`[emote] * ${formatted.slice(7, d)} ${formatted.slice(d + 1)}`);
      } else {
        formatted = `${display}: ${text}`;
        console.log(`[chat]  ${display}: ${text}`);
      }

      addHistory(formatted);

      for (const client of clients) {
        if (client !== ws) client.send(formatted);
      }
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

console.log(`FalloutChat server  port=${PORT}  max_history=${MAX_HISTORY}`);
