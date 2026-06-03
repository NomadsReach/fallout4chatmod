'use strict';
const { WebSocketServer, WebSocket } = require('ws');

const PORT        = parseInt(process.env.PORT        || '3000', 10);
const MAX_HISTORY = parseInt(process.env.MAX_HISTORY || '50',   10);

// { ts: 'HH:MM:SS', raw: '<broadcast-formatted string>' }
const history = [];
const clients = new Set();

function nowTs() {
  return new Date().toTimeString().slice(0, 8);
}

function addHistory(raw) {
  history.push({ ts: nowTs(), raw });
  if (history.length > MAX_HISTORY) history.shift();
}

function broadcastCount() {
  const msg = `[COUNT]:${clients.size}`;
  for (const ws of clients) {
    if (ws.readyState === WebSocket.OPEN) ws.send(msg);
  }
}

const wss = new WebSocketServer({ port: PORT });

wss.on('connection', ws => {
  clients.add(ws);
  console.log(`[+] connected  total=${clients.size}`);

  // Send message history to new client
  for (const h of history) {
    if (ws.readyState === WebSocket.OPEN)
      ws.send(`[HISTORY]${h.ts}|${h.raw}`);
  }

  broadcastCount();

  ws.on('message', data => {
    const msg = data.toString();

    // Rename command — update nothing, no broadcast
    if (msg.startsWith('[RENAME]')) {
      console.log(`[rename] ${msg.slice(8)}`);
      return;
    }

    let formatted;

    if (msg.startsWith('[EMOTE]')) {
      // Already formatted by client: [EMOTE]sender\x01action
      formatted = msg;
    } else {
      // Expected: steamID|username[|location]: text
      const ci = msg.indexOf(': ');
      if (ci === -1) {
        console.warn(`[bad msg] missing ': '  raw=${msg.slice(0, 80)}`);
        return;
      }

      const prefix   = msg.slice(0, ci);
      const text     = msg.slice(ci + 2).trim();
      if (!text) return;

      const parts    = prefix.split('|');
      const steamId  = parts[0] ?? '0';
      const username = (parts[1] ?? 'Player').trim();
      // parts[2] = location — not broadcast, used for server-side info only

      // Web clients send steamId '0'; mark them so they're distinguishable
      const display = steamId === '0' ? `[Web] ${username}` : username;

      // /me emote
      if (text.startsWith('/me ')) {
        formatted = `[EMOTE]${display}\x01${text.slice(4)}`;
      } else {
        formatted = `${display}: ${text}`;
      }
    }

    // Log
    if (formatted.startsWith('[EMOTE]')) {
      const d = formatted.indexOf('\x01');
      console.log(`[emote] * ${formatted.slice(7, d)} ${formatted.slice(d + 1)}`);
    } else {
      const d = formatted.indexOf(': ');
      if (d !== -1) console.log(`[chat]  ${formatted.slice(0, d)}: ${formatted.slice(d + 2)}`);
    }

    addHistory(formatted);

    for (const client of clients) {
      if (client !== ws && client.readyState === WebSocket.OPEN)
        client.send(formatted);
    }
  });

  ws.on('close', () => {
    clients.delete(ws);
    console.log(`[-] disconnected  total=${clients.size}`);
    broadcastCount();
  });

  ws.on('error', err => {
    console.error(`[ws error] ${err.message}`);
    clients.delete(ws);
  });
});

console.log(`FalloutChat server  port=${PORT}  max_history=${MAX_HISTORY}`);
