/*! \file ws_gmcp.cpp
 * \brief GMCP telnet-subnegotiation to WebSocket-JSON bridge implementation.
 * \author Diablerie@COR (2026)
 */

#include "ws_gmcp.h"

#include <algorithm>
#include <cstring>

#ifdef _WIN32
#  include <winsock2.h>
#else
#  include <sys/socket.h>
#  include <unistd.h>
#endif

// ---------------------------------------------------------------------------
// Internal: JSON string escaping
// ---------------------------------------------------------------------------
static std::string json_escape(std::string_view s)
{
    std::string out;
    out.reserve(s.size() + 4);
    for (const unsigned char c : s)
    {
        switch (c)
        {
        case '"':  out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\b': out += "\\b";  break;
        case '\f': out += "\\f";  break;
        case '\n': out += "\\n";  break;
        case '\r': out += "\\r";  break;
        case '\t': out += "\\t";  break;
        default:
            if (c < 0x20)
            {
                // Control character: \uXXXX
                char buf[8];
                snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                out += static_cast<char>(c);
            }
            break;
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Internal: minimal "find string value for key" JSON parser.
// Only handles top-level string values; sufficient for GMCP messages.
// ---------------------------------------------------------------------------
static std::optional<std::string> json_string_value(std::string_view json,
                                                      std::string_view key)
{
    // Build search needle: "key":
    std::string needle;
    needle  = '"';
    needle += key;
    needle += '"';

    const auto kpos = json.find(needle);
    if (kpos == std::string_view::npos) return std::nullopt;

    // Skip past key and colon
    auto pos = kpos + needle.size();
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != ':') return std::nullopt;
    ++pos;
    while (pos < json.size() && (json[pos] == ' ' || json[pos] == '\t')) ++pos;
    if (pos >= json.size() || json[pos] != '"') return std::nullopt;
    ++pos; // past opening quote

    // Read string value with escape handling
    std::string value;
    while (pos < json.size())
    {
        const char c = json[pos];
        if (c == '"') break; // closing quote
        if (c == '\\')
        {
            ++pos;
            if (pos >= json.size()) return std::nullopt;
            switch (json[pos])
            {
            case '"':  value += '"';  break;
            case '\\': value += '\\'; break;
            case 'n':  value += '\n'; break;
            case 'r':  value += '\r'; break;
            case 't':  value += '\t'; break;
            case 'b':  value += '\b'; break;
            case 'f':  value += '\f'; break;
            default:   value += json[pos]; break;
            }
        }
        else
        {
            value += c;
        }
        ++pos;
    }
    return value;
}

// ---------------------------------------------------------------------------
// gmcp_to_ws_json
// ---------------------------------------------------------------------------
std::string gmcp_to_ws_json(const GmcpMessage &msg)
{
    std::string out;
    out.reserve(64 + msg.package.size() + msg.data.size());
    out  = "{\"type\":\"gmcp\",\"package\":\"";
    out += json_escape(msg.package);
    out += "\",\"data\":\"";
    out += json_escape(msg.data);
    out += "\"}";
    return out;
}

// ---------------------------------------------------------------------------
// ws_json_to_gmcp
// ---------------------------------------------------------------------------
std::optional<GmcpMessage> ws_json_to_gmcp(std::string_view json)
{
    auto package = json_string_value(json, "package");
    if (!package) return std::nullopt;

    auto data = json_string_value(json, "data");
    if (!data) return std::nullopt;

    return GmcpMessage{std::move(*package), std::move(*data)};
}

// ---------------------------------------------------------------------------
// GmcpFilter
// ---------------------------------------------------------------------------
GmcpFilter::GmcpFilter(TextCb on_text, GmcpCb on_gmcp)
    : m_on_text(std::move(on_text))
    , m_on_gmcp(std::move(on_gmcp))
{}

void GmcpFilter::feed(std::string_view bytes, int mux_fd)
{
    feed(reinterpret_cast<const uint8_t *>(bytes.data()), bytes.size(), mux_fd);
}

void GmcpFilter::send_reply(int fd, uint8_t cmd, uint8_t opt)
{
    if (fd < 0) return;
    const uint8_t reply[3] = { telnet::IAC, cmd, opt };
#ifdef _WIN32
    send(fd, reinterpret_cast<const char *>(reply), 3, 0);
#else
    (void)write(fd, reply, 3);
#endif
}

void GmcpFilter::flush_text()
{
    if (!m_text_buf.empty())
    {
        m_on_text(m_text_buf);
        m_text_buf.clear();
    }
}

void GmcpFilter::dispatch_sb()
{
    if (!m_in_gmcp_sb || m_sb_buf.empty()) return;

    // SB payload starts with the option byte (GMCP = 0xC9), already stripped.
    // Format: "Package.Name <json-data>"
    const auto sp = m_sb_buf.find(' ');
    GmcpMessage msg;
    if (sp == std::string::npos)
    {
        msg.package = m_sb_buf;
        msg.data    = "";
    }
    else
    {
        msg.package = m_sb_buf.substr(0, sp);
        msg.data    = m_sb_buf.substr(sp + 1);
    }
    flush_text();
    m_on_gmcp(msg);
    m_sb_buf.clear();
    m_in_gmcp_sb = false;
}

void GmcpFilter::feed(const uint8_t *data, size_t len, int mux_fd)
{
    for (size_t i = 0; i < len; ++i)
    {
        const uint8_t b = data[i];

        switch (m_state)
        {
        // ----------------------------------------------------------------
        case State::Normal:
            if (b == telnet::IAC)
                m_state = State::HaveIAC;
            else
                m_text_buf += static_cast<char>(b);
            break;

        // ----------------------------------------------------------------
        case State::HaveIAC:
            if (b == telnet::IAC)
            {
                // IAC IAC → literal 0xFF in the data stream
                m_text_buf += static_cast<char>(0xFF);
                m_state = State::Normal;
            }
            else if (b == telnet::WILL) { m_state = State::HaveIACWill; }
            else if (b == telnet::WONT) { m_state = State::HaveIACWont; }
            else if (b == telnet::DO)   { m_state = State::HaveIACDo;   }
            else if (b == telnet::DONT) { m_state = State::HaveIACDont; }
            else if (b == telnet::SB)
            {
                m_in_gmcp_sb  = false;
                m_seen_sb_opt = false;
                m_sb_buf.clear();
                m_state = State::HaveIACSB;
            }
            else
            {
                // Other 2-byte IAC commands (NOP, GA, etc.) — swallow
                m_state = State::Normal;
            }
            break;

        // ----------------------------------------------------------------
        case State::HaveIACWill:
            // Remote end says it WILL do option b.
            // For GMCP we should respond DO GMCP; refuse everything else.
            if (b == telnet::GMCP)
            {
                // MUX is offering GMCP — accept it
                send_reply(mux_fd, telnet::DO, telnet::GMCP);
                m_gmcp_enabled = true;
            }
            else
            {
                send_reply(mux_fd, telnet::DONT, b);
            }
            m_state = State::Normal;
            break;

        // ----------------------------------------------------------------
        case State::HaveIACWont:
            // Remote WONT do option — just acknowledge, no reply needed
            m_state = State::Normal;
            break;

        // ----------------------------------------------------------------
        case State::HaveIACDo:
            // Remote asking us to DO option b — refuse everything except
            // passthrough (the MUX handles its own DO handling for non-GMCP)
            // For WebSocket we only advertise GMCP support.
            if (b != telnet::GMCP)
            {
                send_reply(mux_fd, telnet::WONT, b);
            }
            m_state = State::Normal;
            break;

        // ----------------------------------------------------------------
        case State::HaveIACDont:
            // Remote asking us to DONT — swallow
            m_state = State::Normal;
            break;

        // ----------------------------------------------------------------
        case State::HaveIACSB:
            if (b == telnet::IAC)
            {
                m_state = State::HaveIACSBIAC;
            }
            else if (!m_seen_sb_opt)
            {
                // First byte of SB payload is always the option code.
                // Use a dedicated flag so subsequent bytes aren't mistaken
                // for another option byte when sb_buf is still empty.
                m_in_gmcp_sb  = (b == telnet::GMCP);
                m_seen_sb_opt = true;
            }
            else if (m_in_gmcp_sb)
            {
                m_sb_buf += static_cast<char>(b);
            }
            break;

        // ----------------------------------------------------------------
        case State::HaveIACSBIAC:
            if (b == telnet::SE)
            {
                dispatch_sb();
                m_state = State::Normal;
            }
            else if (b == telnet::IAC)
            {
                // IAC IAC inside SB = literal 0xFF
                if (m_in_gmcp_sb) m_sb_buf += static_cast<char>(0xFF);
                m_state = State::HaveIACSB;
            }
            else
            {
                m_state = State::Normal;
            }
            break;
        } // switch
    }

    // Flush any remaining plain text
    flush_text();
}
