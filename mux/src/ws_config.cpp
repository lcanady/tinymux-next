/*! \file ws_config.cpp
 * \brief WebSocket bridge configuration implementation.
 * \author Diablerie@COR (2026)
 */

#include "ws_config.h"

#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

WsConfig g_ws_config;

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------
static std::string trim(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

static bool parse_bool(std::string_view s)
{
    std::string lo(s);
    for (auto &c : lo) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return lo == "yes" || lo == "true" || lo == "1" || lo == "on";
}

// ---------------------------------------------------------------------------
// ws_config_load
// ---------------------------------------------------------------------------
void ws_config_load(const char *path)
{
    std::ifstream f(path);
    if (!f.is_open()) return;  // silently use defaults

    std::string line;
    while (std::getline(f, line))
    {
        // Strip comments
        const auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        line = trim(line);
        if (line.empty()) continue;

        const auto eq = line.find('=');
        if (eq == std::string::npos)
        {
            // Try whitespace separator (key value)
            std::istringstream iss(line);
            std::string key, val;
            if (!(iss >> key >> val)) continue;
            line = key + "=" + val;
        }

        const std::string key = trim(line.substr(0, line.find('=')));
        const std::string val = trim(line.substr(line.find('=') + 1));
        if (key.empty() || val.empty()) continue;

        if      (key == "ws_enabled")     g_ws_config.enabled     = parse_bool(val);
        else if (key == "ws_port")        g_ws_config.ws_port      = static_cast<uint16_t>(std::stoul(val));
        else if (key == "wss_port")       g_ws_config.wss_port     = static_cast<uint16_t>(std::stoul(val));
        else if (key == "ws_certfile")    g_ws_config.cert_file    = val;
        else if (key == "ws_keyfile")     g_ws_config.key_file     = val;
        else if (key == "ws_bind")        g_ws_config.bind_addr    = val;
        else if (key == "ws_max_clients") g_ws_config.max_clients  = std::stoi(val);
        // Unknown keys are silently ignored
    }
}

// ---------------------------------------------------------------------------
// ws_config_tls_valid
// ---------------------------------------------------------------------------
bool ws_config_tls_valid() noexcept
{
    if (g_ws_config.wss_port == 0) return false;
    if (g_ws_config.cert_file.empty() || g_ws_config.key_file.empty()) return false;
    try
    {
        return std::filesystem::exists(g_ws_config.cert_file)
            && std::filesystem::exists(g_ws_config.key_file);
    }
    catch (...) { return false; }
}
