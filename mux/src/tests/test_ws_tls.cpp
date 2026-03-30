/*! \file test_ws_tls.cpp
 * \brief Catch2 tests for ws_config.h/cpp and TLS configuration.
 */

#include <catch2/catch_all.hpp>
#include "../ws_config.h"

#include <cstdio>
#include <filesystem>
#include <fstream>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
struct TempFile
{
    fs::path path;
    explicit TempFile(const std::string &content = "")
    {
        path = fs::temp_directory_path() / ("ws_test_" + std::to_string(
            std::chrono::steady_clock::now().time_since_epoch().count()));
        if (!content.empty())
        {
            std::ofstream f(path);
            f << content;
        }
    }
    ~TempFile() { fs::remove(path); }
    TempFile(const TempFile &)            = delete;
    TempFile &operator=(const TempFile &) = delete;
};

// Reset global config to defaults before each test section.
static void reset_config()
{
    g_ws_config = WsConfig{};
}

// ---------------------------------------------------------------------------
// ws_config_load
// ---------------------------------------------------------------------------
TEST_CASE("ws_config_load — reads ws_port", "[config]")
{
    reset_config();
    TempFile tf("ws_port = 5678\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.ws_port == 5678);
}

TEST_CASE("ws_config_load — reads wss_port", "[config]")
{
    reset_config();
    TempFile tf("wss_port = 8443\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.wss_port == 8443);
}

TEST_CASE("ws_config_load — reads cert and key paths", "[config]")
{
    reset_config();
    TempFile tf("ws_certfile = /etc/ssl/cert.pem\nws_keyfile = /etc/ssl/key.pem\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.cert_file == "/etc/ssl/cert.pem");
    REQUIRE(g_ws_config.key_file  == "/etc/ssl/key.pem");
}

TEST_CASE("ws_config_load — reads ws_enabled = no", "[config]")
{
    reset_config();
    TempFile tf("ws_enabled = no\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.enabled == false);
}

TEST_CASE("ws_config_load — reads ws_max_clients", "[config]")
{
    reset_config();
    TempFile tf("ws_max_clients = 42\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.max_clients == 42);
}

TEST_CASE("ws_config_load — missing file uses defaults", "[config]")
{
    reset_config();
    ws_config_load("/nonexistent/path/ws.conf");
    REQUIRE(g_ws_config.ws_port     == 4202);
    REQUIRE(g_ws_config.wss_port    == 0);
    REQUIRE(g_ws_config.cert_file.empty());
    REQUIRE(g_ws_config.enabled     == true);
}

TEST_CASE("ws_config_load — comment lines are ignored", "[config]")
{
    reset_config();
    TempFile tf("# this is a comment\nws_port = 9999\n# another comment\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.ws_port == 9999);
}

TEST_CASE("ws_config_load — malformed lines are skipped", "[config]")
{
    reset_config();
    TempFile tf("this has no equals sign\nws_port = 1234\n=no_key\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.ws_port == 1234);
}

TEST_CASE("ws_config_load — whitespace-separated key value syntax", "[config]")
{
    reset_config();
    TempFile tf("ws_port     4567\n");
    ws_config_load(tf.path.c_str());
    REQUIRE(g_ws_config.ws_port == 4567);
}

// ---------------------------------------------------------------------------
// ws_config_tls_valid
// ---------------------------------------------------------------------------
TEST_CASE("ws_config_tls_valid — false when wss_port is 0", "[config][tls]")
{
    reset_config();
    g_ws_config.wss_port  = 0;
    g_ws_config.cert_file = "/etc/ssl/cert.pem";
    g_ws_config.key_file  = "/etc/ssl/key.pem";
    REQUIRE_FALSE(ws_config_tls_valid());
}

TEST_CASE("ws_config_tls_valid — false when cert_file is empty", "[config][tls]")
{
    reset_config();
    g_ws_config.wss_port  = 4443;
    g_ws_config.cert_file = "";
    g_ws_config.key_file  = "/etc/ssl/key.pem";
    REQUIRE_FALSE(ws_config_tls_valid());
}

TEST_CASE("ws_config_tls_valid — false when key_file is empty", "[config][tls]")
{
    reset_config();
    g_ws_config.wss_port  = 4443;
    g_ws_config.cert_file = "/etc/ssl/cert.pem";
    g_ws_config.key_file  = "";
    REQUIRE_FALSE(ws_config_tls_valid());
}

TEST_CASE("ws_config_tls_valid — false when cert file does not exist", "[config][tls]")
{
    reset_config();
    g_ws_config.wss_port  = 4443;
    g_ws_config.cert_file = "/nonexistent/cert.pem";
    g_ws_config.key_file  = "/nonexistent/key.pem";
    REQUIRE_FALSE(ws_config_tls_valid());
}

TEST_CASE("ws_config_tls_valid — true when both files exist", "[config][tls]")
{
    reset_config();
    TempFile cert("fake cert content");
    TempFile key("fake key content");

    g_ws_config.wss_port  = 4443;
    g_ws_config.cert_file = cert.path.string();
    g_ws_config.key_file  = key.path.string();

    REQUIRE(ws_config_tls_valid());
}

TEST_CASE("ws_config_tls_valid — false when only cert exists but not key", "[config][tls]")
{
    reset_config();
    TempFile cert("fake cert");

    g_ws_config.wss_port  = 4443;
    g_ws_config.cert_file = cert.path.string();
    g_ws_config.key_file  = "/nonexistent/key.pem";

    REQUIRE_FALSE(ws_config_tls_valid());
}
