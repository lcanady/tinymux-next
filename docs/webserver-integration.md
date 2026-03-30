# Web Server Integration

TinyMUX:Next is a game server, not a web server. It does not serve HTML, handle HTTP routes, or manage sessions. What it *does* provide is a **WebSocket endpoint on port 4202** that any web client can connect to and interact with the game in real time.

Your web server and TinyMUX:Next run side by side. The web server serves your frontend and, optionally, reverse-proxies WebSocket connections so everything runs through a single domain and port.

---

## Table of Contents

- [Architecture](#architecture)
- [What TinyMUX:Next Provides](#what-tinymuxnext-provides)
- [Option A: Direct WebSocket Connection](#option-a-direct-websocket-connection)
- [Option B: Reverse Proxy (Recommended)](#option-b-reverse-proxy-recommended)
  - [nginx](#nginx)
  - [Caddy](#caddy)
- [Web Client Quickstart](#web-client-quickstart)
  - [Plain JavaScript](#plain-javascript)
  - [React Hook](#react-hook)
- [GMCP Integration](#gmcp-integration)
- [Authentication Flow](#authentication-flow)
- [TLS / WSS](#tls--wss)
- [Common Pitfalls](#common-pitfalls)

---

## Architecture

```
Browser
  │
  │  HTTPS (your site)          WSS (game connection)
  │                                    │
  ▼                                    ▼
┌─────────────────────────────────────────────┐
│  Your Web Server  (nginx / Caddy / Node)    │
│                                             │
│  / → serves your HTML/JS/CSS frontend       │
│  /ws → reverse proxy to MUX port 4202       │
└──────────────────────────┬──────────────────┘
                           │ localhost:4202
                           ▼
                  ┌─────────────────┐
                  │  TinyMUX:Next   │
                  │  :4201  telnet  │
                  │  :4202  ws://   │
                  └─────────────────┘
```

The browser connects to your web server's domain (e.g. `wss://play.mygame.com/ws`). nginx (or Caddy) proxies that to `ws://localhost:4202`. The game server sees a normal WebSocket client.

---

## What TinyMUX:Next Provides

| Port | Protocol | What it is |
|------|----------|-----------|
| 4201 | TCP (telnet) | Traditional MUD client connection |
| 4202 | WebSocket (RFC 6455) | Web client connection |

### Wire format on port 4202

All data is **UTF-8 text frames**. There are two kinds:

**Plain game text** — anything the game outputs that is not GMCP:
```
You are standing in the Town Square.
Obvious exits: north, east, south
```

**GMCP JSON frames** — structured data alongside the text stream:
```json
{ "type": "gmcp", "package": "Char.Vitals", "data": "{\"hp\":100,\"max_hp\":100,\"mp\":50}" }
```

The `data` field is always a JSON-encoded string (parse it twice — once for the envelope, once for the payload).

---

## Option A: Direct WebSocket Connection

If you just want to prototype or your site is already on the same machine, connect directly:

```javascript
const ws = new WebSocket("ws://your-server-ip:4202");
```

This works but has drawbacks:
- Exposes port 4202 directly to the internet
- Browsers will block mixing `https://` pages with `ws://` connections
- No path-based routing, no SSL termination

Use this only for local development.

---

## Option B: Reverse Proxy (Recommended)

Run TinyMUX:Next on localhost only and let your web server handle TLS and routing.

### nginx

Install nginx and add a server block. This example serves your site on HTTPS port 443 and proxies `/ws` to the game's WebSocket port:

```nginx
server {
    listen 443 ssl http2;
    server_name play.mygame.com;

    ssl_certificate     /etc/letsencrypt/live/play.mygame.com/fullchain.pem;
    ssl_certificate_key /etc/letsencrypt/live/play.mygame.com/privkey.pem;

    # Serve your web frontend (HTML/JS/CSS)
    root /var/www/mygame;
    index index.html;
    location / {
        try_files $uri $uri/ /index.html;
    }

    # Proxy WebSocket connections to TinyMUX:Next
    location /ws {
        proxy_pass         http://127.0.0.1:4202;
        proxy_http_version 1.1;
        proxy_set_header   Upgrade    $http_upgrade;
        proxy_set_header   Connection "Upgrade";
        proxy_set_header   Host       $host;
        proxy_read_timeout 3600s;   # keep connections open (players idle for hours)
        proxy_send_timeout 3600s;
    }
}

# Redirect HTTP to HTTPS
server {
    listen 80;
    server_name play.mygame.com;
    return 301 https://$host$request_uri;
}
```

In your web client, connect to:
```javascript
const ws = new WebSocket("wss://play.mygame.com/ws");
```

### Caddy

If you prefer Caddy (automatic HTTPS via Let's Encrypt):

```caddy
play.mygame.com {
    # Serve the frontend
    root * /var/www/mygame
    file_server

    # Proxy WebSocket connections to TinyMUX:Next
    handle /ws {
        reverse_proxy 127.0.0.1:4202 {
            transport http {
                versions 1.1
            }
            header_up Upgrade {http.request.header.Upgrade}
            header_up Connection {http.request.header.Connection}
        }
    }
}
```

---

## Web Client Quickstart

### Plain JavaScript

A minimal terminal-style client:

```html
<!DOCTYPE html>
<html>
<head><title>MyMUSH</title></head>
<body>
  <div id="output" style="font-family:monospace; white-space:pre-wrap; height:80vh; overflow-y:auto;"></div>
  <input id="input" type="text" style="width:100%;" placeholder="Type a command...">

  <script>
    const output = document.getElementById('output');
    const input  = document.getElementById('input');

    // Connect through your reverse proxy
    const ws = new WebSocket("wss://play.mygame.com/ws");

    ws.onopen = () => {
      print("--- Connected ---");
    };

    ws.onclose = () => {
      print("--- Disconnected ---");
    };

    ws.onmessage = (event) => {
      let text;
      try {
        const msg = JSON.parse(event.data);
        if (msg.type === "gmcp") {
          handleGmcp(msg.package, JSON.parse(msg.data));
          return;  // don't print GMCP frames as raw text
        }
        text = event.data;
      } catch {
        text = event.data;   // not JSON, plain game text
      }
      print(text);
    };

    function handleGmcp(pkg, data) {
      // Handle structured game data here
      if (pkg === "Char.Vitals") {
        document.title = `HP: ${data.hp}/${data.max_hp}`;
      }
    }

    function print(text) {
      output.textContent += text + "\n";
      output.scrollTop = output.scrollHeight;
    }

    // Send commands on Enter
    input.addEventListener('keydown', (e) => {
      if (e.key === 'Enter' && input.value.trim()) {
        ws.send(input.value + "\r\n");
        input.value = '';
      }
    });
  </script>
</body>
</html>
```

### React Hook

A reusable hook for a React frontend:

```typescript
// hooks/useMux.ts
import { useEffect, useRef, useState, useCallback } from "react";

export type GmcpMessage = { type: "gmcp"; package: string; data: unknown };
export type TextMessage  = { type: "text"; text: string };
export type MuxMessage   = GmcpMessage | TextMessage;

export function useMux(url: string) {
  const ws = useRef<WebSocket | null>(null);
  const [connected, setConnected] = useState(false);
  const [messages, setMessages]   = useState<MuxMessage[]>([]);

  useEffect(() => {
    const socket = new WebSocket(url);
    ws.current = socket;

    socket.onopen  = () => setConnected(true);
    socket.onclose = () => setConnected(false);

    socket.onmessage = (event) => {
      try {
        const msg = JSON.parse(event.data as string);
        if (msg.type === "gmcp") {
          setMessages(prev => [...prev, {
            type:    "gmcp",
            package: msg.package,
            data:    JSON.parse(msg.data as string),
          }]);
          return;
        }
      } catch { /* not JSON */ }

      setMessages(prev => [...prev, { type: "text", text: event.data as string }]);
    };

    return () => socket.close();
  }, [url]);

  const send = useCallback((cmd: string) => {
    ws.current?.send(cmd + "\r\n");
  }, []);

  const sendGmcp = useCallback((pkg: string, data: unknown) => {
    ws.current?.send(JSON.stringify({
      type:    "gmcp",
      package: pkg,
      data:    JSON.stringify(data),
    }));
  }, []);

  return { connected, messages, send, sendGmcp };
}
```

Usage in a component:

```typescript
// components/GameTerminal.tsx
import { useMux } from "../hooks/useMux";

export function GameTerminal() {
  const { connected, messages, send } = useMux("wss://play.mygame.com/ws");
  const [input, setInput] = useState("");

  const handleKey = (e: React.KeyboardEvent) => {
    if (e.key === "Enter") {
      send(input);
      setInput("");
    }
  };

  return (
    <div>
      <div className="terminal">
        {messages
          .filter(m => m.type === "text")
          .map((m, i) => <div key={i}>{(m as TextMessage).text}</div>)
        }
      </div>
      <input
        value={input}
        onChange={e => setInput(e.target.value)}
        onKeyDown={handleKey}
        placeholder={connected ? "Type a command..." : "Connecting..."}
        disabled={!connected}
      />
    </div>
  );
}
```

---

## GMCP Integration

GMCP lets the game push structured data (stats, room info, inventory) to your web client without embedding it in the text stream. See [`docs/websocket.md`](websocket.md) for the full wire format.

### Receiving GMCP (game → client)

```javascript
ws.onmessage = (event) => {
  const msg = JSON.parse(event.data);
  if (msg.type !== "gmcp") return;

  const payload = JSON.parse(msg.data);

  switch (msg.package) {
    case "Char.Vitals":
      updateHpBar(payload.hp, payload.max_hp);
      updateMpBar(payload.mp, payload.max_mp);
      break;
    case "Room.Info":
      updateRoomName(payload.name);
      updateMinimap(payload.id);
      break;
    case "Char.Items.List":
      renderInventory(payload);
      break;
  }
};
```

### Sending GMCP (client → game)

```javascript
function sendGmcp(pkg, data) {
  ws.send(JSON.stringify({
    type:    "gmcp",
    package: pkg,
    data:    JSON.stringify(data),
  }));
}

// Tell the game which GMCP packages your client supports
sendGmcp("Core.Hello", { client: "MyWebClient", version: "1.0" });
sendGmcp("Core.Supports.Set", ["Char 1", "Char.Items 1", "Room 1"]);
```

---

## Authentication Flow

TinyMUX:Next uses the standard MUD `connect` command over the WebSocket. There is no separate HTTP login endpoint — authentication happens in-band over the socket.

```javascript
ws.onopen = () => {
  // The server sends a welcome banner first.
  // After receiving it, send the connect command:
  ws.send(`connect ${username} ${password}\r\n`);
};
```

**Important**: Do not hardcode passwords in your frontend. Collect credentials with a login form and send them over the already-encrypted WSS connection. The game server handles authentication on its end.

If you want a separate web-based login (e.g., OAuth), you would implement that in your own web server and then issue the `connect` command on behalf of the user once they authenticate with your system.

---

## TLS / WSS

Browsers require encrypted WebSocket (`wss://`) when your page is served over `https://`. The reverse proxy approach handles this automatically — your web server terminates TLS and proxies plain `ws://` to localhost.

If you want TinyMUX:Next to terminate TLS directly (no reverse proxy), configure `wss_port` in `netmux.conf`:

```ini
wss_port    4203
ws_certfile /etc/letsencrypt/live/play.mygame.com/fullchain.pem
ws_keyfile  /etc/letsencrypt/live/play.mygame.com/privkey.pem
```

Then connect directly:
```javascript
const ws = new WebSocket("wss://play.mygame.com:4203");
```

The reverse proxy approach is generally preferable because it centralises TLS management and lets you serve the web frontend and the WebSocket from the same domain and port.

---

## Common Pitfalls

**Mixed content blocked**
Your page is `https://` but you're connecting to `ws://`. The browser will block it.
Fix: use the reverse proxy (so you connect to `wss://`) or enable `wss_port` directly.

**Connection drops on idle**
Players leave their browser open for hours. nginx's default `proxy_read_timeout` is 60 seconds. Set it to at least `3600s` (see the nginx config above).

**CORS**
TinyMUX:Next does not serve HTTP — CORS is not relevant for the WebSocket connection itself. CORS only applies to your web server's API routes if you add any.

**Port 4202 exposed publicly**
Bind TinyMUX:Next to localhost only and use a reverse proxy. In `netmux.conf`:
```ini
ws_bind  127.0.0.1
```
This prevents clients from bypassing your proxy and connecting directly.

**Parsing GMCP frames as plain text**
Always `try/catch` around `JSON.parse(event.data)`. If the parse fails, treat it as plain text. If it succeeds and `msg.type === "gmcp"`, handle it as GMCP and don't display it as raw output.

---

## Related Topics

- [`docs/websocket.md`](websocket.md) — WebSocket bridge reference and GMCP wire format
- [`docs/jsonparse.md`](jsonparse.md) — generating structured GMCP data from softcode
- [`docs/testing.md`](testing.md) — running the server locally for frontend development
