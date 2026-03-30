/*! \file ws_proto.cpp
 * \brief RFC 6455 WebSocket protocol codec implementation.
 * \author Diablerie@COR (2026)
 */

#include "ws_proto.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <stdexcept>

#include <openssl/bio.h>
#include <openssl/buffer.h>
#include <openssl/evp.h>

// ---------------------------------------------------------------------------
// Internal: case-insensitive header value extraction
// ---------------------------------------------------------------------------
static std::string header_value(std::string_view req, std::string_view name)
{
    // Build lowercase versions for comparison
    std::string lo_req(req), lo_name(name);
    std::transform(lo_req.begin(), lo_req.end(), lo_req.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    std::transform(lo_name.begin(), lo_name.end(), lo_name.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });

    std::string needle = lo_name + ":";
    const auto pos = lo_req.find(needle);
    if (pos == std::string::npos) return {};

    const auto val_start = req.find(':', pos) + 1;
    const auto val_end   = req.find("\r\n", val_start);
    if (val_end == std::string_view::npos) return {};

    std::string val{req.substr(val_start, val_end - val_start)};
    // ltrim
    const auto first = val.find_first_not_of(" \t");
    if (first == std::string::npos) return {};
    val = val.substr(first);
    // rtrim
    const auto last = val.find_last_not_of(" \t\r\n");
    if (last != std::string::npos) val = val.substr(0, last + 1);
    return val;
}

// ---------------------------------------------------------------------------
// ws_compute_accept_key
// ---------------------------------------------------------------------------
std::string ws_compute_accept_key(std::string_view client_key)
{
    static constexpr std::string_view GUID =
        "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";

    std::string concat;
    concat.reserve(client_key.size() + GUID.size());
    concat.append(client_key);
    concat.append(GUID);

    // SHA-1 via EVP (deprecated SHA1() not used)
    unsigned char digest[EVP_MAX_MD_SIZE] = {};
    unsigned int  digest_len = 0;

    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    if (!ctx) return {};
    if (EVP_DigestInit_ex(ctx, EVP_sha1(), nullptr) != 1 ||
        EVP_DigestUpdate(ctx, concat.data(), concat.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, digest, &digest_len) != 1)
    {
        EVP_MD_CTX_free(ctx);
        return {};
    }
    EVP_MD_CTX_free(ctx);

    // Base64 (no newlines)
    BIO *b64  = BIO_new(BIO_f_base64());
    BIO *bmem = BIO_new(BIO_s_mem());
    if (!b64 || !bmem)
    {
        BIO_free(b64);
        BIO_free(bmem);
        return {};
    }
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);
    b64 = BIO_push(b64, bmem); // b64 owns bmem now
    BIO_write(b64, digest, static_cast<int>(digest_len));
    BIO_flush(b64);

    BUF_MEM *bptr = nullptr;
    BIO_get_mem_ptr(b64, &bptr);
    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);
    return result;
}

// ---------------------------------------------------------------------------
// ws_parse_http_upgrade
// ---------------------------------------------------------------------------
std::optional<std::string> ws_parse_http_upgrade(std::string_view request)
{
    // Must end with \r\n\r\n to be complete
    if (request.find("\r\n\r\n") == std::string_view::npos) return std::nullopt;

    // Must be a GET request
    if (request.substr(0, 4) != "GET ") return std::nullopt;

    // Upgrade: websocket (case-insensitive)
    std::string upgrade = header_value(request, "Upgrade");
    std::transform(upgrade.begin(), upgrade.end(), upgrade.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (upgrade != "websocket") return std::nullopt;

    // Connection: Upgrade
    std::string connection = header_value(request, "Connection");
    std::transform(connection.begin(), connection.end(), connection.begin(),
                   [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (connection.find("upgrade") == std::string::npos) return std::nullopt;

    // Sec-WebSocket-Key: required
    std::string key = header_value(request, "Sec-WebSocket-Key");
    if (key.empty()) return std::nullopt;

    return key;
}

// ---------------------------------------------------------------------------
// ws_build_101_response
// ---------------------------------------------------------------------------
std::string ws_build_101_response(std::string_view accept_key,
                                   std::string_view subprotocol)
{
    std::string resp;
    resp.reserve(256);
    resp  = "HTTP/1.1 101 Switching Protocols\r\n";
    resp += "Upgrade: websocket\r\n";
    resp += "Connection: Upgrade\r\n";
    resp += "Sec-WebSocket-Accept: ";
    resp += accept_key;
    resp += "\r\n";
    if (!subprotocol.empty())
    {
        resp += "Sec-WebSocket-Protocol: ";
        resp += subprotocol;
        resp += "\r\n";
    }
    resp += "\r\n";
    return resp;
}

// ---------------------------------------------------------------------------
// WsEncoder
// ---------------------------------------------------------------------------
std::vector<uint8_t> WsEncoder::encode(WsOpcode opcode, std::string_view payload)
{
    return encode(opcode,
                  reinterpret_cast<const uint8_t *>(payload.data()),
                  payload.size());
}

std::vector<uint8_t> WsEncoder::encode(WsOpcode opcode,
                                        const uint8_t *data, size_t len)
{
    std::vector<uint8_t> frame;
    frame.reserve(10 + len);

    // Byte 0: FIN=1, RSV=0, opcode
    frame.push_back(0x80 | static_cast<uint8_t>(opcode));

    // Byte 1+: payload length (server never masks, MASK bit = 0)
    if (len <= 125)
    {
        frame.push_back(static_cast<uint8_t>(len));
    }
    else if (len <= 65535)
    {
        frame.push_back(126);
        frame.push_back(static_cast<uint8_t>((len >> 8) & 0xFF));
        frame.push_back(static_cast<uint8_t>(len & 0xFF));
    }
    else
    {
        frame.push_back(127);
        for (int i = 7; i >= 0; --i)
            frame.push_back(static_cast<uint8_t>((len >> (8 * i)) & 0xFF));
    }

    // Payload
    frame.insert(frame.end(), data, data + len);
    return frame;
}

std::vector<uint8_t> WsEncoder::text(std::string_view payload)
{
    return encode(WsOpcode::Text, payload);
}

std::vector<uint8_t> WsEncoder::close(uint16_t status_code, std::string_view reason)
{
    std::vector<uint8_t> payload;
    payload.push_back(static_cast<uint8_t>((status_code >> 8) & 0xFF));
    payload.push_back(static_cast<uint8_t>(status_code & 0xFF));
    payload.insert(payload.end(),
                   reinterpret_cast<const uint8_t *>(reason.data()),
                   reinterpret_cast<const uint8_t *>(reason.data()) + reason.size());
    return encode(WsOpcode::Close, payload.data(), payload.size());
}

std::vector<uint8_t> WsEncoder::pong(const std::vector<uint8_t> &ping_payload)
{
    return encode(WsOpcode::Pong, ping_payload.data(), ping_payload.size());
}

// ---------------------------------------------------------------------------
// WsDecoder
// ---------------------------------------------------------------------------
void WsDecoder::feed(std::string_view data)
{
    feed(reinterpret_cast<const uint8_t *>(data.data()), data.size());
}

void WsDecoder::feed(const uint8_t *data, size_t len)
{
    if (m_error) return;
    m_pending.insert(m_pending.end(), data, data + len);
    process();
}

std::optional<WsFrame> WsDecoder::next_frame()
{
    if (m_ready.empty()) return std::nullopt;
    WsFrame f = std::move(m_ready.front());
    m_ready.erase(m_ready.begin());
    return f;
}

void WsDecoder::reset() noexcept
{
    m_state        = State::ReadHeader1;
    m_error        = false;
    m_fin          = true;
    m_masked       = false;
    m_payload_len  = 0;
    m_payload_read = 0;
    m_ext_read     = 0;
    m_payload_buf.clear();
    m_pending.clear();
    m_ready.clear();
    std::memset(m_mask, 0, sizeof(m_mask));
    std::memset(m_ext_buf, 0, sizeof(m_ext_buf));
}

void WsDecoder::process()
{
    size_t i = 0;
    while (i < m_pending.size() && !m_error)
    {
        const uint8_t byte = m_pending[i];

        switch (m_state)
        {
        case State::ReadHeader1:
            m_fin    = (byte & 0x80) != 0;
            m_opcode = static_cast<WsOpcode>(byte & 0x0F);
            // RSV bits must be 0 (no extensions negotiated)
            if (byte & 0x70) { m_error = true; break; }
            m_state = State::ReadHeader2;
            ++i;
            break;

        case State::ReadHeader2:
            m_masked      = (byte & 0x80) != 0;
            m_payload_len = byte & 0x7F;
            m_ext_read    = 0;
            std::memset(m_ext_buf, 0, sizeof(m_ext_buf));

            if (m_payload_len == 126)
                m_state = State::ReadExtLen16;
            else if (m_payload_len == 127)
                m_state = State::ReadExtLen64;
            else
            {
                m_state = m_masked ? State::ReadMask : State::ReadPayload;
                m_payload_buf.clear();
                m_payload_buf.reserve(m_payload_len);
                m_payload_read = 0;
            }
            ++i;
            break;

        case State::ReadExtLen16:
            m_ext_buf[m_ext_read++] = byte;
            ++i;
            if (m_ext_read == 2)
            {
                m_payload_len = (static_cast<uint64_t>(m_ext_buf[0]) << 8)
                              |  static_cast<uint64_t>(m_ext_buf[1]);
                m_state = m_masked ? State::ReadMask : State::ReadPayload;
                m_payload_buf.clear();
                m_payload_buf.reserve(static_cast<size_t>(
                    std::min(m_payload_len, uint64_t{1024 * 1024})));
                m_payload_read = 0;
            }
            break;

        case State::ReadExtLen64:
            m_ext_buf[m_ext_read++] = byte;
            ++i;
            if (m_ext_read == 8)
            {
                m_payload_len = 0;
                for (int k = 0; k < 8; ++k)
                    m_payload_len = (m_payload_len << 8) | m_ext_buf[k];
                m_state = m_masked ? State::ReadMask : State::ReadPayload;
                m_payload_buf.clear();
                m_payload_read = 0;
            }
            break;

        case State::ReadMask:
            m_mask[m_ext_read++] = byte;
            ++i;
            if (m_ext_read == 4)
            {
                m_state        = State::ReadPayload;
                m_payload_buf.clear();
                m_payload_buf.reserve(static_cast<size_t>(
                    std::min(m_payload_len, uint64_t{1024 * 1024})));
                m_payload_read = 0;
            }
            break;

        case State::ReadPayload:
        {
            // Consume as many payload bytes as available in one shot
            const uint64_t remaining = m_payload_len - m_payload_read;
            const size_t   avail     = m_pending.size() - i;
            const size_t   take      = static_cast<size_t>(
                std::min(remaining, static_cast<uint64_t>(avail)));

            const size_t old_size = m_payload_buf.size();
            m_payload_buf.resize(old_size + take);
            std::memcpy(m_payload_buf.data() + old_size,
                        m_pending.data() + i, take);

            if (m_masked)
            {
                for (size_t k = 0; k < take; ++k)
                    m_payload_buf[old_size + k] ^=
                        m_mask[(m_payload_read + k) % 4];
            }
            m_payload_read += take;
            i              += take;

            if (m_payload_read == m_payload_len)
                dispatch_frame();
            break;
        }
        } // switch
    }

    // Remove consumed bytes
    if (i > 0)
        m_pending.erase(m_pending.begin(),
                        m_pending.begin() + static_cast<ptrdiff_t>(i));
}

void WsDecoder::dispatch_frame()
{
    WsFrame f;
    f.opcode  = m_opcode;
    f.fin     = m_fin;
    f.payload = std::move(m_payload_buf);
    m_ready.push_back(std::move(f));

    // Reset for next frame
    m_state        = State::ReadHeader1;
    m_payload_buf  = {};
    m_payload_read = 0;
    m_payload_len  = 0;
    m_masked       = false;
    m_ext_read     = 0;
    std::memset(m_mask, 0, sizeof(m_mask));
    std::memset(m_ext_buf, 0, sizeof(m_ext_buf));
}

// ---------------------------------------------------------------------------
// ws_state
// ---------------------------------------------------------------------------
ws_state::ws_state() : decoder(new WsDecoder()) {}

ws_state::~ws_state()
{
    delete decoder;
    decoder = nullptr;
}
