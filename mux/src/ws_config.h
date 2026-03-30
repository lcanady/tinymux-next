/*! \file ws_config.h
 * \brief WebSocket bridge configuration.
 *
 * Parameters are loaded from ws.conf (located in the game directory,
 * alongside netmux.conf).
 */

#pragma once
#ifndef WS_CONFIG_H
#define WS_CONFIG_H

#include <cstdint>
#include <string>

struct WsConfig
{
    bool        enabled     = true;
    uint16_t    ws_port     = 4202;   // 0 = disabled
    uint16_t    wss_port    = 0;      // 0 = disabled (TLS WebSocket)
    std::string cert_file;            // PEM certificate path (wss only)
    std::string key_file;             // PEM private key path  (wss only)
    std::string bind_addr;            // empty = INADDR_ANY
    int         max_clients = 100;
};

// Global singleton — populated by ws_config_load().
extern WsConfig g_ws_config;

// Load configuration from the given file path.
// Silently uses defaults if the file does not exist.
void ws_config_load(const char *path);

// True if wss:// is configured with existing cert + key files.
[[nodiscard]] bool ws_config_tls_valid() noexcept;

#endif // WS_CONFIG_H
