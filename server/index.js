'use strict';

const PORT        = parseInt(process.env.PORT        || '3000', 10);
const MAX_HISTORY = parseInt(process.env.MAX_HISTORY || '50',   10);
const MAX_MSG_LEN = 600;   // hard cap on raw incoming message bytes
const MAX_USERNAME_LEN = 32;
const RATE_LIMIT_MS = 500; // minimum ms between messages per client

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
      clients.add(ws);
      console.log(`[+] connected  total=${clients.size}`);

      for (const h of history) {
        ws.send(`[HISTORY]${h.ts}|${h.raw}`);
      }

      broadcastCount();
    },

    message(ws, data) {
      // 1. Length cap
      if (data.length > MAX_MSG_LEN) {
        console.warn(`[drop] oversized message (${data.length} bytes)`);
        return;
      }

      // 2. Rate limit
      if (isRateLimited(ws)) {
        console.warn('[drop] rate limited');
        return;
      }

      const msg = String(data);

      // 3. Rename — silent, no broadcast
      if (msg.startsWith('[RENAME]')) {
        console.log(`[rename] ${msg.slice(8, 80)}`);
        return;
      }

      // 4. Reject raw [EMOTE] from clients — only the server builds these
      if (msg.startsWith('[EMOTE]')) {
        console.warn('[drop] client tried to inject [EMOTE] directly');
        return;
      }

      // 5. Parse: steamID|username[|location]: text
      const ci = msg.indexOf(': ');
      if (ci === -1) {
        console.warn(`[drop] no ': ' delimiter`);
        return;
      }

      const prefix = msg.slice(0, ci);
      const text   = sanitize(msg.slice(ci + 2));
      if (!text || text.length > 500) return;

      const parts    = prefix.split('|');
      const steamId  = sanitize(parts[0] ?? '0');
      const rawName  = sanitize(parts[1] ?? 'Player').slice(0, MAX_USERNAME_LEN);

      // Strip any spoofed system prefix from username
      const username = stripSpoofedPrefixes(rawName) || 'Player';
      const display  = steamId === '0' ? `[Web] ${username}` : username;

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
