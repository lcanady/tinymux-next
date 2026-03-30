/*! \file test_ws_gmcp.cpp
 * \brief Catch2 tests for ws_gmcp.h/cpp — GMCP telnet bridge.
 */

#include <catch2/catch_all.hpp>
#include "../ws_gmcp.h"

#include <chrono>
#include <thread>
#include <sys/socket.h>
#include <unistd.h>

// Helper: create a connected socketpair, return {server_fd, client_fd}
static std::pair<int,int> make_socketpair()
{
    int fds[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, fds) != 0)
        throw std::runtime_error("socketpair failed");
    return {fds[0], fds[1]};
}

static std::string read_all_nonblock(int fd)
{
    // Set non-blocking, drain, restore
    char buf[1024];
    std::string result;
    ssize_t n;
    // Short timeout via MSG_DONTWAIT
    while ((n = recv(fd, buf, sizeof(buf), MSG_DONTWAIT)) > 0)
        result.append(buf, static_cast<size_t>(n));
    return result;
}

// ---------------------------------------------------------------------------
// gmcp_to_ws_json
// ---------------------------------------------------------------------------
TEST_CASE("gmcp_to_ws_json — basic package and data", "[gmcp][serialize]")
{
    GmcpMessage msg{"Core.Supports.Set", "[\"GMCP\", 1]"};
    const std::string json = gmcp_to_ws_json(msg);
    REQUIRE(json.find("\"type\":\"gmcp\"")      != std::string::npos);
    REQUIRE(json.find("\"package\":\"Core.Supports.Set\"") != std::string::npos);
    REQUIRE(json.find("\"data\":")              != std::string::npos);
}

TEST_CASE("gmcp_to_ws_json — JSON special chars in data are escaped", "[gmcp][serialize]")
{
    GmcpMessage msg{"Test", "\"quoted\" and \\backslash\\"};
    const std::string json = gmcp_to_ws_json(msg);
    // The data field value must have escaped quotes and backslashes
    REQUIRE(json.find("\\\"quoted\\\"") != std::string::npos);
    REQUIRE(json.find("\\\\backslash\\\\") != std::string::npos);
}

TEST_CASE("gmcp_to_ws_json — empty data produces empty data field", "[gmcp][serialize]")
{
    GmcpMessage msg{"Core.Ping", ""};
    const std::string json = gmcp_to_ws_json(msg);
    REQUIRE(json.find("\"data\":\"\"") != std::string::npos);
}

TEST_CASE("gmcp_to_ws_json — newline in data is escaped", "[gmcp][serialize]")
{
    GmcpMessage msg{"Test", "line1\nline2"};
    const std::string json = gmcp_to_ws_json(msg);
    REQUIRE(json.find("\\n") != std::string::npos);
    // Raw newline must not appear in JSON
    REQUIRE(json.find('\n') == std::string::npos);
}

// ---------------------------------------------------------------------------
// ws_json_to_gmcp
// ---------------------------------------------------------------------------
TEST_CASE("ws_json_to_gmcp — roundtrip with gmcp_to_ws_json", "[gmcp][deserialize]")
{
    GmcpMessage original{"Char.Vitals", "{\"hp\":100}"};
    const std::string json  = gmcp_to_ws_json(original);
    const auto decoded      = ws_json_to_gmcp(json);
    REQUIRE(decoded.has_value());
    REQUIRE(decoded->package == original.package);
    REQUIRE(decoded->data    == original.data);
}

TEST_CASE("ws_json_to_gmcp — malformed JSON returns nullopt", "[gmcp][deserialize]")
{
    REQUIRE_FALSE(ws_json_to_gmcp("not json at all").has_value());
    REQUIRE_FALSE(ws_json_to_gmcp("{broken").has_value());
    REQUIRE_FALSE(ws_json_to_gmcp("").has_value());
}

TEST_CASE("ws_json_to_gmcp — missing package field returns nullopt", "[gmcp][deserialize]")
{
    REQUIRE_FALSE(ws_json_to_gmcp("{\"type\":\"gmcp\",\"data\":\"x\"}").has_value());
}

TEST_CASE("ws_json_to_gmcp — missing data field returns nullopt", "[gmcp][deserialize]")
{
    REQUIRE_FALSE(ws_json_to_gmcp("{\"type\":\"gmcp\",\"package\":\"X\"}").has_value());
}

// ---------------------------------------------------------------------------
// GmcpFilter — plain text passthrough
// ---------------------------------------------------------------------------
TEST_CASE("GmcpFilter — plain text passes through unchanged", "[gmcp][filter]")
{
    std::string text_out;
    std::vector<GmcpMessage> gmcp_out;

    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [&](const GmcpMessage &m){ gmcp_out.push_back(m); }
    );

    filter.feed("Hello, world!\r\n", -1);
    REQUIRE(text_out == "Hello, world!\r\n");
    REQUIRE(gmcp_out.empty());
}

// ---------------------------------------------------------------------------
// GmcpFilter — GMCP subnegotiation extraction
// ---------------------------------------------------------------------------
static std::vector<uint8_t> make_gmcp_sb(std::string_view payload)
{
    // IAC SB GMCP <payload> IAC SE
    std::vector<uint8_t> out;
    out.push_back(telnet::IAC);
    out.push_back(telnet::SB);
    out.push_back(telnet::GMCP);
    for (const char c : payload) out.push_back(static_cast<uint8_t>(c));
    out.push_back(telnet::IAC);
    out.push_back(telnet::SE);
    return out;
}

TEST_CASE("GmcpFilter — GMCP subneg extracted, surrounding text preserved", "[gmcp][filter]")
{
    std::string text_out;
    std::vector<GmcpMessage> gmcp_out;

    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [&](const GmcpMessage &m){ gmcp_out.push_back(m); }
    );

    const std::string before = "before";
    const std::string after  = "after";
    auto sb = make_gmcp_sb("Core.Ping {}");

    std::vector<uint8_t> data;
    data.insert(data.end(), before.begin(), before.end());
    data.insert(data.end(), sb.begin(), sb.end());
    data.insert(data.end(), after.begin(), after.end());

    filter.feed(std::string_view(reinterpret_cast<const char *>(data.data()),
                                  data.size()), -1);
    REQUIRE(text_out == "beforeafter");
    REQUIRE(gmcp_out.size() == 1);
    REQUIRE(gmcp_out[0].package == "Core.Ping");
    REQUIRE(gmcp_out[0].data    == "{}");
}

TEST_CASE("GmcpFilter — GMCP subneg split across two feed() calls", "[gmcp][filter]")
{
    std::string text_out;
    std::vector<GmcpMessage> gmcp_out;

    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [&](const GmcpMessage &m){ gmcp_out.push_back(m); }
    );

    auto sb = make_gmcp_sb("Room.Info {\"id\":1}");
    const size_t split = sb.size() / 2;

    filter.feed(std::string_view(reinterpret_cast<const char *>(sb.data()), split), -1);
    REQUIRE(gmcp_out.empty()); // not complete yet

    filter.feed(std::string_view(reinterpret_cast<const char *>(sb.data() + split),
                                  sb.size() - split), -1);
    REQUIRE(gmcp_out.size() == 1);
    REQUIRE(gmcp_out[0].package == "Room.Info");
}

TEST_CASE("GmcpFilter — multiple GMCP messages in one feed", "[gmcp][filter]")
{
    std::string text_out;
    std::vector<GmcpMessage> gmcp_out;

    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [&](const GmcpMessage &m){ gmcp_out.push_back(m); }
    );

    auto sb1 = make_gmcp_sb("Char.Name {\"name\":\"Hero\"}");
    auto sb2 = make_gmcp_sb("Char.Vitals {\"hp\":50}");

    std::vector<uint8_t> combined;
    combined.insert(combined.end(), sb1.begin(), sb1.end());
    combined.insert(combined.end(), sb2.begin(), sb2.end());

    filter.feed(std::string_view(reinterpret_cast<const char *>(combined.data()),
                                  combined.size()), -1);
    REQUIRE(gmcp_out.size() == 2);
    REQUIRE(gmcp_out[0].package == "Char.Name");
    REQUIRE(gmcp_out[1].package == "Char.Vitals");
}

// ---------------------------------------------------------------------------
// GmcpFilter — telnet negotiation responses (uses socketpair)
// ---------------------------------------------------------------------------
TEST_CASE("GmcpFilter — IAC WILL GMCP triggers DO GMCP reply", "[gmcp][filter][negotiation]")
{
    auto [srv, cli] = make_socketpair();

    std::string text_out;
    std::vector<GmcpMessage> gmcp_out;
    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [&](const GmcpMessage &m){ gmcp_out.push_back(m); }
    );

    // IAC WILL GMCP
    const uint8_t will_gmcp[] = { telnet::IAC, telnet::WILL, telnet::GMCP };
    filter.feed(std::string_view(reinterpret_cast<const char *>(will_gmcp),
                                  sizeof(will_gmcp)), srv);

    REQUIRE(filter.gmcp_enabled());

    // srv should have received IAC DO GMCP from filter
    // Give a short time for the write to land
    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const std::string reply = read_all_nonblock(cli);
    REQUIRE(reply.size() == 3);
    REQUIRE(static_cast<uint8_t>(reply[0]) == telnet::IAC);
    REQUIRE(static_cast<uint8_t>(reply[1]) == telnet::DO);
    REQUIRE(static_cast<uint8_t>(reply[2]) == telnet::GMCP);

    close(srv); close(cli);
}

TEST_CASE("GmcpFilter — IAC WILL X (non-GMCP) triggers DONT reply", "[gmcp][filter][negotiation]")
{
    auto [srv, cli] = make_socketpair();

    GmcpFilter filter(
        [](std::string_view){},
        [](const GmcpMessage &){}
    );

    const uint8_t will_ttype[] = { telnet::IAC, telnet::WILL, 0x18 }; // TTYPE
    filter.feed(std::string_view(reinterpret_cast<const char *>(will_ttype),
                                  sizeof(will_ttype)), srv);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const std::string reply = read_all_nonblock(cli);
    REQUIRE(reply.size() == 3);
    REQUIRE(static_cast<uint8_t>(reply[1]) == telnet::DONT);
    REQUIRE(static_cast<uint8_t>(reply[2]) == 0x18);

    close(srv); close(cli);
}

TEST_CASE("GmcpFilter — IAC DO X (non-GMCP) triggers WONT reply", "[gmcp][filter][negotiation]")
{
    auto [srv, cli] = make_socketpair();

    GmcpFilter filter(
        [](std::string_view){},
        [](const GmcpMessage &){}
    );

    const uint8_t do_echo[] = { telnet::IAC, telnet::DO, 0x01 }; // ECHO
    filter.feed(std::string_view(reinterpret_cast<const char *>(do_echo),
                                  sizeof(do_echo)), srv);

    std::this_thread::sleep_for(std::chrono::milliseconds(10));
    const std::string reply = read_all_nonblock(cli);
    REQUIRE(reply.size() == 3);
    REQUIRE(static_cast<uint8_t>(reply[1]) == telnet::WONT);

    close(srv); close(cli);
}

TEST_CASE("GmcpFilter — IAC IAC passes through as literal 0xFF byte", "[gmcp][filter]")
{
    std::string text_out;
    GmcpFilter filter(
        [&](std::string_view s){ text_out += s; },
        [](const GmcpMessage &){}
    );

    const uint8_t data[] = { 'A', telnet::IAC, telnet::IAC, 'B' };
    filter.feed(std::string_view(reinterpret_cast<const char *>(data), sizeof(data)), -1);

    REQUIRE(text_out.size() == 3);
    REQUIRE(text_out[0] == 'A');
    REQUIRE(static_cast<uint8_t>(text_out[1]) == 0xFF);
    REQUIRE(text_out[2] == 'B');
}
