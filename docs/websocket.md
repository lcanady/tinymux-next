# WebSocket Bridge

TinyMUX opens a second listening port (default **4202**) that accepts RFC 6455 WebSocket connections. Any WebSocket client can connect and interact with the game exactly as a standard telnet/MUSH client would — the bridge is transparent.

GMCP telnet subnegotiations are translated into JSON WebSocket frames and back, enabling modern web clients to receive rich structured data.

---

## Table of Contents

- [How It Works](#how-it-works)
- [Configuration (ws.conf)](#configuration-wsconf)
- [Connecting](#connecting)
- [GMCP over WebSocket](#gmcp-over-websocket)
- [TLS WebSocket (WSS)](#tls-websocket-wss)
- [Implementation Notes](#implementation-notes)

---

## How It Works

```
WebSocket Client                  TinyMUX
      |                               |
      |  WS handshake (HTTP 101)      |
      |------------------------------>|  ws_proto: HTTP upgrade
      |                               |
      |  WS text frame: "look"        |
      |------------------------------>|  ws_proto: unwrap frame
      |                               |  → game command pipeline
      |                               |  ← game output
      |  WS text frame: "You see..."  |
      |<------------------------------|  ws_proto: wrap frame
      |                               |
      |  WS text frame (GMCP JSON):   |
      |  {"type":"gmcp",              |  ws_gmcp: encode
      |   "package":"Char.Vitals",    |
      |   "data":"{\"hp\":100}"}      |
      |<------------------------------|
```

The flow:
1. Client opens a TCP connection to `ws_port` and performs an HTTP/1.1 WebSocket upgrade handshake.
2. The server responds with `101 Switching Protocols` and the `Sec-WebSocket-Accept` header.
3. From that point, all game output is sent as WebSocket text frames; all client input arrives as WebSocket text frames.
4. GMCP subnegotiations from the game are intercepted by `GmcpFilter`, serialised as JSON, and sent as WebSocket frames. Incoming GMCP JSON frames are deserialised and injected back into the telnet stream.

---

## Configuration (netmux.conf)

WebSocket settings live at the bottom of your main `netmux.conf` — no separate file needed:

```ini
# WebSocket bridge configuration
# All settings are optional; shown values are defaults.

ws_enabled     yes      # Set to "no" to disable the bridge entirely
ws_port        4202     # Plain (unencrypted) WebSocket port; 0 = disabled
wss_port       0        # TLS WebSocket port; 0 = disabled
ws_certfile               # Path to PEM certificate file (required when wss_port > 0)
ws_keyfile                # Path to PEM private key file  (required when wss_port > 0)
ws_bind                   # Interface to bind to; empty = all interfaces (INADDR_ANY)
ws_max_clients 100      # Maximum simultaneous WebSocket connections
```

### Minimal config (plain WebSocket on 4202)

```ini
ws_enabled  yes
ws_port     4202
```

### Disable WebSocket entirely

```ini
ws_enabled  no
```

### Enable WSS (TLS WebSocket) only

```ini
ws_enabled  yes
ws_port     0
wss_port    4203
ws_certfile /etc/ssl/certs/mymush.crt
ws_keyfile  /etc/ssl/private/mymush.key
```

---

## Connecting

### Browser (JavaScript)

```javascript
const ws = new WebSocket("ws://your-server:4202");

ws.onopen = () => {
    console.log("Connected");
};

ws.onmessage = (event) => {
    // event.data is the game output text (or GMCP JSON frame)
    console.log("Received:", event.data);
};

// Send a command
ws.send("connect YourCharacter YourPassword\r\n");
ws.send("look\r\n");
```

### Python (websockets library)

```python
import asyncio
import websockets

async def play():
    async with websockets.connect("ws://your-server:4202") as ws:
        await ws.send("connect Wizard password\r\n")
        response = await ws.recv()
        print(response)

asyncio.run(play())
```

### Testing with wscat

```bash
npm install -g wscat
wscat -c ws://localhost:4202
```

---

## GMCP over WebSocket

[GMCP](https://wiki.mudlet.org/w/Manual:GMCP) (Generic MUD Communication Protocol) carries structured data alongside the text stream using telnet subnegotiations. The WebSocket bridge intercepts these and converts them to JSON.

### Frame format

All GMCP frames are JSON text frames with this shape:

```json
{
  "type": "gmcp",
  "package": "Package.SubPackage",
  "data": "<JSON string or empty>"
}
```

The `data` field is always a JSON-encoded string (i.e., it may itself be a JSON object, but it arrives as a string value in the outer envelope).

### Game → Client (receiving GMCP)

When the game sends a GMCP subnegotiation, the bridge delivers it as a WebSocket frame:

```json
{ "type": "gmcp", "package": "Char.Vitals", "data": "{\"hp\":100,\"max_hp\":100,\"mp\":50}" }
{ "type": "gmcp", "package": "Room.Info",   "data": "{\"name\":\"The Town Square\",\"id\":1}" }
{ "type": "gmcp", "package": "Char.Items.List", "data": "[{\"id\":1,\"name\":\"sword\"}]" }
```

In JavaScript, parse with:

```javascript
ws.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.type === "gmcp") {
        const payload = JSON.parse(msg.data);
        if (msg.package === "Char.Vitals") {
            updateHealthBar(payload.hp, payload.max_hp);
        }
    } else {
        appendToOutput(event.data);  // plain game text
    }
};
```

### Client → Game (sending GMCP)

Send a JSON frame in the same format to inject a GMCP subnegotiation into the game stream:

```javascript
ws.send(JSON.stringify({
    type: "gmcp",
    package: "Core.Hello",
    data: JSON.stringify({ client: "MyClient", version: "1.0" })
}));
```

### Telnet negotiation

When the game advertises `WILL GMCP`, the bridge responds `DO GMCP` automatically and sets `gmcp_enabled = true`. Client code does not need to handle the telnet negotiation layer.

---

## TLS WebSocket (WSS)

To enable encrypted WebSocket connections:

1. **Obtain a certificate and key** (e.g., from Let's Encrypt):
   ```bash
   certbot certonly --standalone -d your.domain.com
   # Produces: /etc/letsencrypt/live/your.domain.com/fullchain.pem
   #           /etc/letsencrypt/live/your.domain.com/privkey.pem
   ```

2. **Add to netmux.conf**:
   ```ini
   ws_enabled  yes
   ws_port     4202    # keep plain WS if you want both
   wss_port    4203
   ws_certfile /etc/letsencrypt/live/your.domain.com/fullchain.pem
   ws_keyfile  /etc/letsencrypt/live/your.domain.com/privkey.pem
   ```

3. **Connect from a browser**:
   ```javascript
   const ws = new WebSocket("wss://your.domain.com:4203");
   ```

The server requires `--enable-ssl` at build time for WSS. The `wss_port` option is silently ignored if OpenSSL is not available.

---

## Implementation Notes

### Source files

| File | Description |
|---|---|
| `mux/src/ws_proto.cpp/h` | RFC 6455 codec: handshake, frame encoder/decoder |
| `mux/src/ws_gmcp.cpp/h` | GMCP↔JSON bridge with telnet state machine |
| `mux/src/ws_config.cpp/h` | `ws.conf` parser; `WsConfig` struct; global `g_ws_config` |
| `mux/src/bsd.cpp` | Integration point: WS listen socket, accept loop, per-connection state |

### Per-connection state (`ws_state`)

Each WebSocket connection carries a `ws_state` struct:

```cpp
struct ws_state {
    std::string  http_buf;       // accumulates HTTP upgrade request
    WsDecoder    decoder;        // stateful RFC 6455 frame decoder
    bool         gmcp_enabled;   // set when game advertises WILL GMCP
};
```

### Output cap

WebSocket text frames are limited to the server's LBUF size (8000 bytes) to match the MUX output buffer. Very long game output is naturally split across multiple `think` / output ticks.

### Related Topics

- [`docs/testing.md`](testing.md) — verifying the WebSocket bridge with the Docker suite
- In-game: `wizhelp websocket`
