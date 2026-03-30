/*! \file http_logic.h
 * \brief HttpClient — HTTP result types and pure-logic helpers.
 *
 * This header contains code with no TinyMUX or libcurl dependency so it can
 * be included by the Catch2 test binary as well as by httpclient.cpp.
 */
#ifndef HTTP_LOGIC_H
#define HTTP_LOGIC_H

#include <cstddef>
#include <cstdio>
#include <string>

static constexpr std::size_t HTTP_LBUF_SIZE  = 8000;
static constexpr long        HTTP_TIMEOUT_S  = 30L;
static constexpr long        HTTP_CONNECT_S  = 10L;
static constexpr long        HTTP_MAX_REDIRS = 10L;

// Result from an HTTP request
struct HttpResult
{
    bool        ok     = false;
    long        status = 0;
    std::string body;
    std::string errmsg;
};

// libcurl CURLOPT_WRITEFUNCTION — accumulates response bytes into a std::string.
// Caps accumulation at 2×HTTP_LBUF_SIZE to prevent unbounded memory growth.
inline std::size_t curl_write_cb(char *ptr, std::size_t size, std::size_t nmemb, void *userdata)
{
    const std::size_t total = size * nmemb;
    auto *buf = static_cast<std::string *>(userdata);
    if (buf->size() + total <= HTTP_LBUF_SIZE * 2)
        buf->append(ptr, total);
    return total;  // always return total — libcurl interprets less as an error
}

// Write an HttpResult to a MUX output buffer.
// CharT is char in tests, unsigned char (UTF8) in the module.
template<typename CharT>
inline void emit_result(const HttpResult &res, CharT *buff, CharT **bufc)
{
    auto tp        = *bufc;
    const auto end = buff + HTTP_LBUF_SIZE - 1;

    auto emit_s = [&](const char *s) {
        while (tp < end && *s) *tp++ = static_cast<CharT>(*s++);
    };

    if (!res.ok)
    {
        emit_s("#-1 CURL ");
        emit_s(res.errmsg.c_str());
    }
    else if (res.status < 200 || res.status >= 300)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "#-1 HTTP %ld", res.status);
        emit_s(buf);
    }
    else
    {
        const char *src = res.body.c_str();
        while (tp < end && *src) *tp++ = static_cast<CharT>(*src++);
    }

    *bufc = tp;
}

#endif // HTTP_LOGIC_H
