/*! \file ws_proto.h
 * \brief RFC 6455 WebSocket protocol codec.
 *
 * Standalone, zero-dependency (except OpenSSL for accept-key computation).
 * All types are C++17 RAII-safe. No raw new/delete.
 */

#pragma once
#ifndef WS_PROTO_H
#define WS_PROTO_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

// ---------------------------------------------------------------------------
// WebSocket opcodes (RFC 6455 §5.2)
// ---------------------------------------------------------------------------
enum class WsOpcode : uint8_t
{
    Continuation = 0x00,
    Text         = 0x01,
    Binary       = 0x02,
    Close        = 0x08,
    Ping         = 0x09,
    Pong         = 0x0A,
};

// ---------------------------------------------------------------------------
// Decoded WebSocket frame
// ---------------------------------------------------------------------------
struct WsFrame
{
    WsOpcode             opcode  = WsOpcode::Text;
    bool                 fin     = true;
    std::vector<uint8_t> payload;

    // Convenience: payload as string_view (valid while WsFrame lives)
    [[nodiscard]] std::string_view text() const noexcept
    {
        return {reinterpret_cast<const char *>(payload.data()), payload.size()};
    }
};

// ---------------------------------------------------------------------------
// WsEncoder — encodes frames for sending to the client.
// Server frames are never masked (RFC 6455 §5.1).
// ---------------------------------------------------------------------------
class WsEncoder
{
public:
    // Encode a single final frame.
    [[nodiscard]] static std::vector<uint8_t>
    encode(WsOpcode opcode, std::string_view payload);

    // Convenience overload for binary payload.
    [[nodiscard]] static std::vector<uint8_t>
    encode(WsOpcode opcode, const uint8_t *data, size_t len);

    // Encode a text frame (most common case).
    [[nodiscard]] static std::vector<uint8_t>
    text(std::string_view payload);

    // Encode a close frame with optional 2-byte status code.
    [[nodiscard]] static std::vector<uint8_t>
    close(uint16_t status_code = 1000, std::string_view reason = {});

    // Encode a pong frame in response to a ping.
    [[nodiscard]] static std::vector<uint8_t>
    pong(const std::vector<uint8_t> &ping_payload);
};

// ---------------------------------------------------------------------------
// WsDecoder — stateful incremental decoder.
//
// Feed raw bytes in arbitrarily-sized chunks (mirroring TCP fragmentation).
// Call next_frame() to pull completed frames.
//
// Thread-compatible: one decoder per connection, not shared.
// ---------------------------------------------------------------------------
class WsDecoder
{
public:
    WsDecoder() = default;

    // Push bytes received from the network.
    void feed(std::string_view data);
    void feed(const uint8_t *data, size_t len);

    // Pull the next complete frame, or nullopt if none ready yet.
    [[nodiscard]] std::optional<WsFrame> next_frame();

    // True if the decoder is in an unrecoverable error state.
    [[nodiscard]] bool error() const noexcept { return m_error; }

    // Reset state (e.g., after a close frame).
    void reset() noexcept;

private:
    enum class State : uint8_t
    {
        ReadHeader1,   // byte 0: FIN + RSV + opcode
        ReadHeader2,   // byte 1: MASK + payload-len-7
        ReadExtLen16,  // extended 16-bit length
        ReadExtLen64,  // extended 64-bit length
        ReadMask,      // 4-byte masking key
        ReadPayload,   // payload bytes
    };

    State    m_state   = State::ReadHeader1;
    bool     m_error   = false;

    // Current frame being assembled
    WsOpcode m_opcode  = WsOpcode::Text;
    bool     m_fin     = true;
    bool     m_masked  = false;
    uint64_t m_payload_len  = 0;
    uint64_t m_payload_read = 0;
    uint8_t  m_mask[4] = {};
    uint8_t  m_ext_buf[8] = {};
    uint8_t  m_ext_read    = 0;
    std::vector<uint8_t> m_payload_buf;

    // Completed frames waiting to be consumed
    std::vector<WsFrame> m_ready;

    // Pending raw bytes not yet consumed by the state machine
    std::vector<uint8_t> m_pending;

    void process();
    void dispatch_frame();
};

// ---------------------------------------------------------------------------
// HTTP upgrade handshake helpers
// ---------------------------------------------------------------------------

// Compute the Sec-WebSocket-Accept header value.
// RFC 6455 §4.2.2: base64(SHA1(client_key + GUID))
// Returns empty string on OpenSSL failure.
[[nodiscard]] std::string
ws_compute_accept_key(std::string_view client_key);

// Parse an HTTP/1.1 Upgrade: websocket request.
// Returns the Sec-WebSocket-Key value, or nullopt if the request is not a
// valid WebSocket upgrade.
[[nodiscard]] std::optional<std::string>
ws_parse_http_upgrade(std::string_view request);

// Build an HTTP 101 Switching Protocols response.
// subprotocol is optional; pass empty to omit the header.
[[nodiscard]] std::string
ws_build_101_response(std::string_view accept_key,
                       std::string_view subprotocol = {});

// ---------------------------------------------------------------------------
// ws_state — per-connection state embedded in DESC.
//
// Plain struct (no STL containers inside) so it is safe with MUX pool
// allocator. Allocated with new/delete by the WS integration layer.
// ---------------------------------------------------------------------------
struct ws_state
{
    // Accumulate the HTTP upgrade request (max 8 KiB)
    static constexpr size_t HTTP_BUF_MAX = 8192;
    char   http_buf[HTTP_BUF_MAX] = {};
    size_t http_buf_len            = 0;

    // Per-connection decoder (heap-allocated so DESC stays plain)
    WsDecoder *decoder    = nullptr;
    bool       gmcp_active = false;

    ws_state();
    ~ws_state();

    // Non-copyable, non-movable (owned by DESC)
    ws_state(const ws_state &)            = delete;
    ws_state &operator=(const ws_state &) = delete;
};

#endif // WS_PROTO_H
