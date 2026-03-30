/*! \file mock_mux_server.h
 * \brief Minimal in-process mock MUX telnet server for integration tests.
 *
 * Binds to a random OS-assigned port, accepts one connection, records
 * received bytes, and allows the test to send bytes to the connected client.
 */

#pragma once
#ifndef MOCK_MUX_SERVER_H
#define MOCK_MUX_SERVER_H

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

class MockMuxServer
{
public:
    MockMuxServer()  = default;
    ~MockMuxServer() { stop(); }

    MockMuxServer(const MockMuxServer &)            = delete;
    MockMuxServer &operator=(const MockMuxServer &) = delete;

    /// Bind to a random port and start accepting.
    void start()
    {
        m_listen_fd = ::socket(AF_INET, SOCK_STREAM, 0);
        if (m_listen_fd < 0) throw std::runtime_error("socket() failed");

        int opt = 1;
        setsockopt(m_listen_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

        sockaddr_in addr{};
        addr.sin_family      = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port        = 0; // OS assigns port
        if (::bind(m_listen_fd, reinterpret_cast<sockaddr *>(&addr), sizeof(addr)) < 0)
            throw std::runtime_error("bind() failed");
        if (::listen(m_listen_fd, 1) < 0)
            throw std::runtime_error("listen() failed");

        // Discover assigned port
        socklen_t len = sizeof(addr);
        getsockname(m_listen_fd, reinterpret_cast<sockaddr *>(&addr), &len);
        m_port = ntohs(addr.sin_port);

        m_running.store(true);
        m_thread = std::thread(&MockMuxServer::run, this);
    }

    /// Stop accepting and close all sockets.
    void stop()
    {
        m_running.store(false);
        if (m_listen_fd >= 0) { ::shutdown(m_listen_fd, SHUT_RDWR); ::close(m_listen_fd); m_listen_fd = -1; }
        if (m_client_fd >= 0) { ::shutdown(m_client_fd, SHUT_RDWR); ::close(m_client_fd); m_client_fd = -1; }
        if (m_thread.joinable()) m_thread.join();
    }

    /// Port the server is listening on.
    [[nodiscard]] uint16_t port() const noexcept { return m_port; }

    /// Send bytes to the connected client.
    void send_to_client(std::string_view data)
    {
        std::unique_lock<std::mutex> lk(m_mutex);
        m_cv.wait(lk, [this]{ return m_client_fd >= 0 || !m_running.load(); });
        if (m_client_fd >= 0)
            ::write(m_client_fd, data.data(), data.size());
    }

    /// All bytes received from the client so far.
    [[nodiscard]] std::string received() const
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        return m_received;
    }

    /// Wait (up to 2 s) until at least `n` bytes have been received.
    bool wait_received(size_t n, int timeout_ms = 2000)
    {
        const auto deadline = std::chrono::steady_clock::now()
                            + std::chrono::milliseconds(timeout_ms);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (received().size() >= n) return true;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    [[nodiscard]] bool client_connected() const noexcept
    {
        return m_client_fd >= 0;
    }

private:
    void run()
    {
        sockaddr_in peer{};
        socklen_t   plen = sizeof(peer);
        const int cfd = ::accept(m_listen_fd,
                                  reinterpret_cast<sockaddr *>(&peer), &plen);
        if (cfd < 0) return;

        {
            std::lock_guard<std::mutex> lk(m_mutex);
            m_client_fd = cfd;
        }
        m_cv.notify_all();

        char buf[4096];
        while (m_running.load())
        {
            const ssize_t n = ::recv(cfd, buf, sizeof(buf), 0);
            if (n <= 0) break;
            std::lock_guard<std::mutex> lk(m_mutex);
            m_received.append(buf, static_cast<size_t>(n));
        }
    }

    int                       m_listen_fd = -1;
    int                       m_client_fd = -1;
    uint16_t                  m_port      = 0;
    std::atomic<bool>         m_running{false};
    std::thread               m_thread;
    mutable std::mutex        m_mutex;
    std::condition_variable   m_cv;
    std::string               m_received;
};

#endif // MOCK_MUX_SERVER_H
