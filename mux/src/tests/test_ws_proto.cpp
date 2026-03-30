/*! \file test_ws_proto.cpp
 * \brief Catch2 tests for ws_proto.h/cpp — RFC 6455 WebSocket codec.
 */

#include <catch2/catch_all.hpp>
#include "../ws_proto.h"

#include <cstring>

// ---------------------------------------------------------------------------
// Accept key computation (RFC 6455 §1.3 reference vector)
// ---------------------------------------------------------------------------
TEST_CASE("RFC 6455 accept key — reference vector", "[ws_proto][accept_key]")
{
    // The example from the RFC itself.
    const std::string result =
        ws_compute_accept_key("dGhlIHNhbXBsZSBub25jZQ==");
    REQUIRE(result == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

TEST_CASE("RFC 6455 accept key — empty input returns non-empty", "[ws_proto][accept_key]")
{
    // Empty client key still produces a deterministic SHA-1 result.
    const std::string result = ws_compute_accept_key("");
    REQUIRE(!result.empty());
}

// ---------------------------------------------------------------------------
// HTTP upgrade parsing
// ---------------------------------------------------------------------------
TEST_CASE("HTTP upgrade parsing — valid minimal request", "[ws_proto][http]")
{
    const std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
        "Sec-WebSocket-Version: 13\r\n"
        "\r\n";

    const auto key = ws_parse_http_upgrade(req);
    REQUIRE(key.has_value());
    REQUIRE(*key == "dGhlIHNhbXBsZSBub25jZQ==");
}

TEST_CASE("HTTP upgrade parsing — case-insensitive Upgrade header", "[ws_proto][http]")
{
    const std::string req =
        "GET /chat HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "UPGRADE: WebSocket\r\n"
        "Connection: upgrade\r\n"
        "Sec-WebSocket-Key: abc123==\r\n"
        "\r\n";

    const auto key = ws_parse_http_upgrade(req);
    REQUIRE(key.has_value());
    REQUIRE(*key == "abc123==");
}

TEST_CASE("HTTP upgrade parsing — missing Upgrade header returns nullopt", "[ws_proto][http]")
{
    const std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Connection: keep-alive\r\n"
        "\r\n";
    REQUIRE_FALSE(ws_parse_http_upgrade(req).has_value());
}

TEST_CASE("HTTP upgrade parsing — non-GET method returns nullopt", "[ws_proto][http]")
{
    const std::string req =
        "POST / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "Sec-WebSocket-Key: abc123==\r\n"
        "\r\n";
    REQUIRE_FALSE(ws_parse_http_upgrade(req).has_value());
}

TEST_CASE("HTTP upgrade parsing — missing Sec-WebSocket-Key returns nullopt", "[ws_proto][http]")
{
    const std::string req =
        "GET / HTTP/1.1\r\n"
        "Host: localhost\r\n"
        "Upgrade: websocket\r\n"
        "Connection: Upgrade\r\n"
        "\r\n";
    REQUIRE_FALSE(ws_parse_http_upgrade(req).has_value());
}

TEST_CASE("HTTP upgrade parsing — incomplete request (no CRLFCRLF) returns nullopt", "[ws_proto][http]")
{
    const std::string req =
        "GET / HTTP/1.1\r\n"
        "Upgrade: websocket\r\n";
    REQUIRE_FALSE(ws_parse_http_upgrade(req).has_value());
}

// ---------------------------------------------------------------------------
// 101 response building
// ---------------------------------------------------------------------------
TEST_CASE("101 response contains required headers", "[ws_proto][http]")
{
    const std::string resp = ws_build_101_response("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
    REQUIRE(resp.find("101 Switching Protocols") != std::string::npos);
    REQUIRE(resp.find("Upgrade: websocket")      != std::string::npos);
    REQUIRE(resp.find("Connection: Upgrade")     != std::string::npos);
    REQUIRE(resp.find("s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") != std::string::npos);
    REQUIRE(resp.substr(resp.size() - 4) == "\r\n\r\n");
}

TEST_CASE("101 response with subprotocol includes Sec-WebSocket-Protocol", "[ws_proto][http]")
{
    const std::string resp = ws_build_101_response("key==", "mux");
    REQUIRE(resp.find("Sec-WebSocket-Protocol: mux") != std::string::npos);
}

TEST_CASE("101 response without subprotocol omits Sec-WebSocket-Protocol", "[ws_proto][http]")
{
    const std::string resp = ws_build_101_response("key==");
    REQUIRE(resp.find("Sec-WebSocket-Protocol") == std::string::npos);
}

// ---------------------------------------------------------------------------
// WsEncoder
// ---------------------------------------------------------------------------
TEST_CASE("WsEncoder — 7-bit length text frame", "[ws_proto][encoder]")
{
    const std::string payload = "Hello";
    const auto frame = WsEncoder::text(payload);

    REQUIRE(frame.size() == 2 + payload.size());
    REQUIRE((frame[0] & 0x80) != 0); // FIN set
    REQUIRE((frame[0] & 0x0F) == 0x01); // Text opcode
    REQUIRE((frame[1] & 0x80) == 0); // Not masked (server)
    REQUIRE((frame[1] & 0x7F) == 5); // Length = 5
    REQUIRE(std::string(reinterpret_cast<const char *>(frame.data()) + 2, 5) == payload);
}

TEST_CASE("WsEncoder — 16-bit length frame (126 bytes exactly)", "[ws_proto][encoder]")
{
    const std::string payload(126, 'A');
    const auto frame = WsEncoder::text(payload);

    REQUIRE(frame.size() == 2 + 2 + 126);
    REQUIRE((frame[1] & 0x7F) == 126);
    REQUIRE(frame[2] == 0);
    REQUIRE(frame[3] == 126);
}

TEST_CASE("WsEncoder — 16-bit length frame (65535 bytes)", "[ws_proto][encoder]")
{
    const std::string payload(65535, 'B');
    const auto frame = WsEncoder::encode(WsOpcode::Binary, payload);

    REQUIRE(frame.size() == 2 + 2 + 65535);
    REQUIRE((frame[1] & 0x7F) == 126);
    REQUIRE(frame[2] == 0xFF);
    REQUIRE(frame[3] == 0xFF);
}

TEST_CASE("WsEncoder — 64-bit length frame (65536 bytes)", "[ws_proto][encoder]")
{
    const std::string payload(65536, 'C');
    const auto frame = WsEncoder::text(payload);

    REQUIRE(frame.size() == 2 + 8 + 65536);
    REQUIRE((frame[1] & 0x7F) == 127);
}

TEST_CASE("WsEncoder — close frame with status code", "[ws_proto][encoder]")
{
    const auto frame = WsEncoder::close(1001, "going away");
    REQUIRE((frame[0] & 0x0F) == 0x08); // Close opcode
    // Status code 1001 = 0x03E9
    REQUIRE(frame[2] == 0x03);
    REQUIRE(frame[3] == 0xE9);
    const std::string reason(reinterpret_cast<const char *>(frame.data()) + 4,
                              frame.size() - 4);
    REQUIRE(reason == "going away");
}

TEST_CASE("WsEncoder — ping opcode", "[ws_proto][encoder]")
{
    const auto frame = WsEncoder::encode(WsOpcode::Ping, "ping-data");
    REQUIRE((frame[0] & 0x0F) == 0x09);
}

TEST_CASE("WsEncoder — pong frame mirrors ping payload", "[ws_proto][encoder]")
{
    const std::vector<uint8_t> ping_data = {'p', 'i', 'n', 'g'};
    const auto frame = WsEncoder::pong(ping_data);
    REQUIRE((frame[0] & 0x0F) == 0x0A); // Pong opcode
    REQUIRE(frame.size() == 2 + 4);
    REQUIRE(std::memcmp(frame.data() + 2, ping_data.data(), 4) == 0);
}

// ---------------------------------------------------------------------------
// WsDecoder
// ---------------------------------------------------------------------------
TEST_CASE("WsDecoder — single unmasked text frame", "[ws_proto][decoder]")
{
    // Simulate a server-sent frame (servers don't mask, but decoder handles both)
    const auto encoded = WsEncoder::text("Hello");
    WsDecoder dec;
    dec.feed(std::string_view(reinterpret_cast<const char *>(encoded.data()),
                               encoded.size()));
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->opcode == WsOpcode::Text);
    REQUIRE(frame->fin == true);
    REQUIRE(frame->text() == "Hello");
    REQUIRE_FALSE(dec.next_frame().has_value());
}

TEST_CASE("WsDecoder — masked client frame (RFC 6455 §5.7 example)", "[ws_proto][decoder]")
{
    // "Hello" with masking key 37 fa 21 3d
    // Unmasked: 48 65 6c 6c 6f
    // Masked:   7f 9f 4d 51 58
    const uint8_t frame_bytes[] = {
        0x81,               // FIN, Text
        0x85,               // MASK=1, len=5
        0x37, 0xfa, 0x21, 0x3d, // masking key
        0x7f, 0x9f, 0x4d, 0x51, 0x58 // masked "Hello"
    };
    WsDecoder dec;
    dec.feed(frame_bytes, sizeof(frame_bytes));
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->text() == "Hello");
}

TEST_CASE("WsDecoder — TCP-fragmented frame (byte by byte)", "[ws_proto][decoder]")
{
    const auto encoded = WsEncoder::text("World");
    WsDecoder dec;

    // Feed one byte at a time
    for (const uint8_t b : encoded)
    {
        REQUIRE_FALSE(dec.next_frame().has_value()); // not ready yet
        dec.feed(&b, 1);
    }

    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->text() == "World");
}

TEST_CASE("WsDecoder — multiple frames in one feed call", "[ws_proto][decoder]")
{
    const auto f1 = WsEncoder::text("One");
    const auto f2 = WsEncoder::text("Two");
    std::vector<uint8_t> combined;
    combined.insert(combined.end(), f1.begin(), f1.end());
    combined.insert(combined.end(), f2.begin(), f2.end());

    WsDecoder dec;
    dec.feed(combined.data(), combined.size());

    const auto frame1 = dec.next_frame();
    const auto frame2 = dec.next_frame();
    REQUIRE(frame1.has_value());
    REQUIRE(frame1->text() == "One");
    REQUIRE(frame2.has_value());
    REQUIRE(frame2->text() == "Two");
    REQUIRE_FALSE(dec.next_frame().has_value());
}

TEST_CASE("WsDecoder — 16-bit length frame round-trip", "[ws_proto][decoder]")
{
    const std::string payload(200, 'X');
    const auto encoded = WsEncoder::text(payload);
    WsDecoder dec;
    dec.feed(encoded.data(), encoded.size());
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->text() == payload);
}

TEST_CASE("WsDecoder — 64-bit length frame round-trip", "[ws_proto][decoder]")
{
    const std::string payload(70000, 'Y');
    const auto encoded = WsEncoder::text(payload);
    WsDecoder dec;
    dec.feed(encoded.data(), encoded.size());
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->text() == payload);
}

TEST_CASE("WsDecoder — ping opcode round-trip", "[ws_proto][decoder]")
{
    const auto encoded = WsEncoder::encode(WsOpcode::Ping, "keepalive");
    WsDecoder dec;
    dec.feed(encoded.data(), encoded.size());
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->opcode == WsOpcode::Ping);
    REQUIRE(frame->text() == "keepalive");
}

TEST_CASE("WsDecoder — close frame with status code", "[ws_proto][decoder]")
{
    const auto encoded = WsEncoder::close(1000, "normal");
    WsDecoder dec;
    dec.feed(encoded.data(), encoded.size());
    const auto frame = dec.next_frame();
    REQUIRE(frame.has_value());
    REQUIRE(frame->opcode == WsOpcode::Close);
    // First two bytes of payload are status code 1000 = 0x03E8
    REQUIRE(frame->payload.size() >= 2);
    REQUIRE(frame->payload[0] == 0x03);
    REQUIRE(frame->payload[1] == 0xE8);
}

TEST_CASE("WsDecoder — RSV bits set causes error", "[ws_proto][decoder]")
{
    // RSV1 set — invalid without extension negotiation
    const uint8_t bad_frame[] = { 0xC1, 0x00 }; // FIN + RSV1 + Text, len=0
    WsDecoder dec;
    dec.feed(bad_frame, sizeof(bad_frame));
    REQUIRE(dec.error());
}

TEST_CASE("WsDecoder — reset clears state", "[ws_proto][decoder]")
{
    const auto encoded = WsEncoder::text("test");
    WsDecoder dec;
    dec.feed(encoded.data(), encoded.size());
    REQUIRE(dec.next_frame().has_value());
    dec.reset();
    REQUIRE_FALSE(dec.next_frame().has_value());
    REQUIRE_FALSE(dec.error());
}
