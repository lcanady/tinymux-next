/*! \file ws_gmcp.h
 * \brief GMCP telnet-subnegotiation to WebSocket-JSON bridge.
 *
 * Detects IAC SB GMCP ... IAC SE sequences in the MUX→client byte stream,
 * extracts the package name and data, and routes them separately from plain
 * text so the WebSocket client receives structured JSON frames.
 *
 * Telnet negotiation bytes (DO/WILL/WONT/DONT) are handled here too:
 * - DO GMCP  → reply WILL GMCP (enable GMCP on this session)
 * - WILL X   → reply DONT X   (refuse all other options)
 * - DO X     → reply WONT X   (refuse all other options)
 * - DONT/WONT → swallow
 */

#pragma once
#ifndef WS_GMCP_H
#define WS_GMCP_H

#include <cstdint>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

// ---------------------------------------------------------------------------
// Telnet protocol constants (duplicated here for standalone use)
// ---------------------------------------------------------------------------
namespace telnet {
    constexpr uint8_t IAC  = 0xFF;
    constexpr uint8_t SB   = 0xFA;   // begin subnegotiation
    constexpr uint8_t SE   = 0xF0;   // end subnegotiation
    constexpr uint8_t WILL = 0xFB;
    constexpr uint8_t WONT = 0xFC;
    constexpr uint8_t DO   = 0xFD;
    constexpr uint8_t DONT = 0xFE;
    constexpr uint8_t GMCP = 201;    // 0xC9 — GMCP option code
} // namespace telnet

// ---------------------------------------------------------------------------
// GmcpMessage — a decoded GMCP package + data pair
// ---------------------------------------------------------------------------
struct GmcpMessage
{
    std::string package;  // e.g. "Core.Supports.Set"
    std::string data;     // e.g. "[\"GMCP\", 1]"
};

// Serialize a GmcpMessage to a WebSocket JSON frame body.
// Result: {"type":"gmcp","package":"Core.Supports.Set","data":"..."}
[[nodiscard]] std::string gmcp_to_ws_json(const GmcpMessage &msg);

// Parse a WebSocket JSON frame body back to a GmcpMessage.
// Returns nullopt if the JSON is malformed or missing required fields.
[[nodiscard]] std::optional<GmcpMessage>
ws_json_to_gmcp(std::string_view json);

// ---------------------------------------------------------------------------
// GmcpFilter — stateful stream filter
//
// Processes raw MUX→client bytes. Calls:
//   on_text(string_view)      for every plain-text (non-GMCP) byte run
//   on_gmcp(GmcpMessage)      for every complete GMCP subnegotiation
//
// When a DO GMCP is detected, automatically writes WILL GMCP to mux_fd.
// All other DO/WILL are refused (WONT/DONT sent to mux_fd).
// ---------------------------------------------------------------------------
class GmcpFilter
{
public:
    using TextCb = std::function<void(std::string_view)>;
    using GmcpCb = std::function<void(const GmcpMessage &)>;

    GmcpFilter(TextCb on_text, GmcpCb on_gmcp);

    // Push bytes from MUX. May write negotiation replies to mux_fd.
    void feed(std::string_view bytes, int mux_fd);
    void feed(const uint8_t *data, size_t len, int mux_fd);

    [[nodiscard]] bool gmcp_enabled() const noexcept { return m_gmcp_enabled; }

private:
    enum class State : uint8_t {
        Normal,
        HaveIAC,
        HaveIACWill,
        HaveIACWont,
        HaveIACDo,
        HaveIACDont,
        HaveIACSB,         // inside subnegotiation
        HaveIACSBIAC,      // IAC seen inside subnegotiation (escape or SE)
    };

    State  m_state        = State::Normal;
    bool   m_gmcp_enabled = false;
    bool   m_in_gmcp_sb   = false;  // current SB is for GMCP
    bool   m_seen_sb_opt  = false;  // have we consumed the SB option byte?
    std::string m_sb_buf;           // accumulates SB payload bytes

    TextCb m_on_text;
    GmcpCb m_on_gmcp;

    std::string m_text_buf;  // plain text accumulator

    void flush_text();
    void dispatch_sb();
    static void send_reply(int fd, uint8_t cmd, uint8_t opt);
};

#endif // WS_GMCP_H
