import { Client, GatewayIntentBits, Events } from 'discord.js';

const TOKEN       = process.env.DISCORD_TOKEN;
const CHANNEL_ID  = process.env.DISCORD_CHANNEL_ID;
const WS_URL      = process.env.WS_URL     || 'ws://localhost:3000';
const BOT_SECRET  = process.env.BOT_SECRET || '';

// Moderation gating. A Discord user may run !ban/!unban if they are listed in
// ADMIN_IDS (comma-separated user IDs) OR hold the ADMIN_ROLE_ID role OR have
// the server-wide "Ban Members" permission.
const ADMIN_IDS     = new Set((process.env.ADMIN_IDS || '').split(',').map(s => s.trim()).filter(Boolean));
const ADMIN_ROLE_ID = (process.env.ADMIN_ROLE_ID || '').trim();

if (!TOKEN || !CHANNEL_ID) {
  console.error('[bot] Missing DISCORD_TOKEN or DISCORD_CHANNEL_ID');
  process.exit(1);
}

const REL_DELIM = '\x02';
const HEX64_RE  = /^[0-9a-f]{16}$/i;

// ── Discord ───────────────────────────────────────────────────────────────
// Requires "Message Content" privileged intent enabled in Discord Dev Portal
const discord = new Client({
  intents: [
    GatewayIntentBits.Guilds,
    GatewayIntentBits.GuildMessages,
    GatewayIntentBits.MessageContent,
  ],
});

let channel      = null;
let ws           = null;
let wsReady      = false;
let reconnecting = false;

// Rolling map of recently-seen in-game players: lowercased name -> { id, name, ts }
const recentUsers = new Map();
const RECENT_MAX  = 500;

function rememberUser(id, name) {
  if (!id || id === '0') return; // '0' is the Discord-relay pseudo id
  const key = String(name).toLowerCase();
  recentUsers.set(key, { id, name, ts: Date.now() });
  if (recentUsers.size > RECENT_MAX) {
    // drop oldest
    const oldest = [...recentUsers.entries()].sort((a, b) => a[1].ts - b[1].ts)[0];
    if (oldest) recentUsers.delete(oldest[0]);
  }
}

function resolveTarget(arg) {
  // Explicit id:  "id:<hex>"  or a bare hex64 string
  if (arg.startsWith('id:')) return { id: arg.slice(3).trim(), via: 'id' };
  if (HEX64_RE.test(arg))    return { id: arg, via: 'id' };
  const hit = recentUsers.get(arg.toLowerCase());
  if (hit) return { id: hit.id, name: hit.name, via: 'name' };
  return null;
}

// ── Helpers ───────────────────────────────────────────────────────────────
function escMd(s) {
  return String(s).replace(/([*_`~\\|])/g, '\\$1');
}

function isAdmin(msg) {
  if (ADMIN_IDS.has(msg.author.id)) return true;
  const member = msg.member;
  if (!member) return false;
  if (ADMIN_ROLE_ID && member.roles?.cache?.has(ADMIN_ROLE_ID)) return true;
  if (member.permissions?.has('BanMembers')) return true;
  return false;
}

function relayToDiscord(formatted) {
  if (!channel) return;
  let text;
  if (formatted.startsWith('[EMOTE]')) {
    const d      = formatted.indexOf('\x01');
    const sender = d !== -1 ? formatted.slice(7, d)  : 'Someone';
    const action = d !== -1 ? formatted.slice(d + 1) : '';
    text = `_\\* **${escMd(sender)}** ${escMd(action)}_`;
  } else {
    const ci = formatted.indexOf(': ');
    if (ci === -1) return;
    text = `**${escMd(formatted.slice(0, ci))}**: ${escMd(formatted.slice(ci + 2))}`;
  }
  channel.send(text).catch(err => console.error('[discord] send error:', err));
}

function senderNameOf(formatted) {
  if (formatted.startsWith('[EMOTE]')) {
    const d = formatted.indexOf('\x01');
    return d !== -1 ? formatted.slice(7, d) : '';
  }
  const ci = formatted.indexOf(': ');
  return ci !== -1 ? formatted.slice(0, ci) : '';
}

// ── WebSocket → Discord ───────────────────────────────────────────────────
function connectWS() {
  if (reconnecting) return;
  reconnecting = true;

  ws = new WebSocket(WS_URL);

  ws.onopen = () => {
    wsReady      = true;
    reconnecting = false;
    console.log('[ws] connected to chat server');
    if (BOT_SECRET) ws.send(`[BOT_AUTH]${BOT_SECRET}`);
  };

  ws.onmessage = ({ data }) => {
    const str = String(data);

    // Ignore protocol frames and history on connect
    if (str.startsWith('[COUNT]:') || str.startsWith('[HISTORY]')) return;

    // Moderation acks from the server
    if (str.startsWith('[BANOK]'))   { channel?.send(`✅ ${str.slice(7)}`).catch(() => {}); return; }
    if (str.startsWith('[BANERR]'))  { channel?.send(`⚠️ ${str.slice(8)}`).catch(() => {}); return; }
    if (str.startsWith('[BANLIST]')) {
      const ids = str.slice(9).split(',').filter(Boolean);
      const body = ids.length ? ids.map(id => `\`${id}\``).join(', ') : '_(none)_';
      channel?.send(`🚫 **Banned IDs (${ids.length}):** ${body}`).catch(() => {});
      return;
    }
    if (str.startsWith('[BLOCKEDNAMES]')) {
      const terms = str.slice(14).split(',').filter(Boolean);
      const body = terms.length ? terms.map(t => `\`${t}\``).join(', ') : '_(none)_';
      channel?.send(`🔤 **Blocked name terms (${terms.length}):** ${body}`).catch(() => {});
      return;
    }

    // Relay frames carry the sender's userId: "[REL]<id>\x02<formatted>"
    let formatted = str;
    if (str.startsWith('[REL]')) {
      const d = str.indexOf(REL_DELIM);
      if (d !== -1) {
        const id = str.slice(5, d);
        formatted = str.slice(d + 1);
        rememberUser(id, senderNameOf(formatted));
      } else {
        formatted = str.slice(5);
      }
    }

    relayToDiscord(formatted);
  };

  ws.onclose = () => {
    wsReady      = false;
    reconnecting = false;
    console.log('[ws] disconnected — reconnecting in 5s');
    setTimeout(connectWS, 5000);
  };

  ws.onerror = () => ws.close();
}

function wsSend(payload) {
  if (!wsReady || ws?.readyState !== WebSocket.OPEN) return false;
  ws.send(payload);
  return true;
}

// ── Admin commands ──────────────────────────────────────────────────────────
async function handleCommand(msg) {
  const [cmd, ...rest] = msg.content.trim().split(/\s+/);
  const arg = rest.join(' ').trim();
  const command = cmd.toLowerCase();

  if (!['!ban', '!unban', '!bans', '!whois', '!blockname', '!unblockname', '!blockednames'].includes(command)) return false;

  if (!isAdmin(msg)) {
    await msg.reply('⛔ You do not have permission to moderate the chat.').catch(() => {});
    return true; // handled (consumed, not relayed)
  }

  if (command === '!bans') {
    if (!wsSend('[BANLIST]')) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  if (command === '!whois') {
    if (!arg) { await msg.reply('Usage: `!whois <name>`').catch(() => {}); return true; }
    const hit = recentUsers.get(arg.toLowerCase());
    await msg.reply(hit
      ? `\`${hit.name}\` → \`${hit.id}\``
      : `No recent in-game player named \`${arg}\`. They must have chatted since the bot last started.`
    ).catch(() => {});
    return true;
  }

  if (command === '!ban') {
    if (!arg) { await msg.reply('Usage: `!ban <name>` or `!ban id:<userid>`').catch(() => {}); return true; }
    const target = resolveTarget(arg);
    if (!target || !target.id) {
      await msg.reply(`Could not resolve \`${arg}\`. Use \`!whois <name>\` to find their id, then \`!ban id:<userid>\`.`).catch(() => {});
      return true;
    }
    if (!wsSend(`[BAN]${target.id}`)) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  if (command === '!unban') {
    if (!arg) { await msg.reply('Usage: `!unban <userid>`').catch(() => {}); return true; }
    const id = arg.startsWith('id:') ? arg.slice(3).trim() : arg;
    if (!wsSend(`[UNBAN]${id}`)) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  if (command === '!blockednames') {
    if (!wsSend('[BLOCKEDNAMES]')) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  if (command === '!blockname') {
    if (!arg) { await msg.reply('Usage: `!blockname <word>` — blocks any username containing it (leet-folded).').catch(() => {}); return true; }
    if (!wsSend(`[BLOCKNAME]${arg}`)) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  if (command === '!unblockname') {
    if (!arg) { await msg.reply('Usage: `!unblockname <word>`').catch(() => {}); return true; }
    if (!wsSend(`[UNBLOCKNAME]${arg}`)) await msg.reply('⚠️ Chat server not connected.').catch(() => {});
    return true;
  }

  return false;
}

// ── Discord → WebSocket ───────────────────────────────────────────────────
discord.on(Events.MessageCreate, async msg => {
  if (msg.author.bot)               return; // ignore bots (including itself)
  if (msg.channelId !== CHANNEL_ID) return;
  if (!msg.content.trim())          return;

  // Intercept moderation commands — these are never relayed into the game chat
  if (msg.content.trim().startsWith('!')) {
    const handled = await handleCommand(msg);
    if (handled) return;
  }

  if (!wsReady || ws?.readyState !== WebSocket.OPEN) {
    console.warn('[discord] message received but WS not ready');
    return;
  }

  const name    = msg.member?.displayName ?? msg.author.username;
  const content = msg.content.slice(0, 500);

  // Format: 0|[Discord] Name|Discord: text
  // Server trusts this because BOT_AUTH was sent on connect
  ws.send(`0|[Discord] ${name}|Discord: ${content}`);
});

// ── Boot ──────────────────────────────────────────────────────────────────
discord.once(Events.ClientReady, async () => {
  console.log(`[discord] logged in as ${discord.user.tag}`);

  channel = await discord.channels.fetch(CHANNEL_ID).catch(() => null);
  if (!channel) {
    console.error(`[discord] channel ${CHANNEL_ID} not found — check DISCORD_CHANNEL_ID`);
    process.exit(1);
  }
  console.log(`[discord] relaying #${channel.name} ↔ game chat`);
  console.log(`[discord] admins: ${ADMIN_IDS.size} id(s)${ADMIN_ROLE_ID ? ', role ' + ADMIN_ROLE_ID : ''}, plus Ban Members perm`);
});

connectWS();
discord.login(TOKEN);
