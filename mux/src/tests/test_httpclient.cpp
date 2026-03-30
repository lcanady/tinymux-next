/*! \file test_httpclient.cpp
 * \brief Catch2 unit tests for http_logic.h — emit_result and curl_write_cb.
 *
 * These tests cover the output-formatting and write-callback logic without
 * linking against libcurl or the TinyMUX framework.
 */

#include <catch2/catch_all.hpp>
#include "../modules/http_logic.h"

#include <cstring>
#include <string>

// ---------------------------------------------------------------------------
// Helper: call emit_result and return the output as std::string
// ---------------------------------------------------------------------------
static std::string run_emit(const HttpResult &res)
{
    char buff[HTTP_LBUF_SIZE];
    char *bufc = buff;
    emit_result(res, buff, &bufc);
    *bufc = '\0';
    return std::string(buff, static_cast<std::size_t>(bufc - buff));
}

// ---------------------------------------------------------------------------
// emit_result — curl error path
// ---------------------------------------------------------------------------
TEST_CASE("emit_result — curl failure starts with '#-1 CURL '", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = false;
    res.errmsg = "Could not resolve host";
    const std::string out = run_emit(res);
    REQUIRE(out == "#-1 CURL Could not resolve host");
}

TEST_CASE("emit_result — curl failure with empty errmsg", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = false;
    res.errmsg = "";
    const std::string out = run_emit(res);
    REQUIRE(out == "#-1 CURL ");
}

// ---------------------------------------------------------------------------
// emit_result — HTTP error status codes
// ---------------------------------------------------------------------------
TEST_CASE("emit_result — HTTP 404 produces '#-1 HTTP 404'", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 404;
    res.body   = "<html>Not Found</html>";
    REQUIRE(run_emit(res) == "#-1 HTTP 404");
}

TEST_CASE("emit_result — HTTP 500 produces '#-1 HTTP 500'", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 500;
    REQUIRE(run_emit(res) == "#-1 HTTP 500");
}

TEST_CASE("emit_result — HTTP 301 (redirect) produces '#-1 HTTP 301'", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 301;
    REQUIRE(run_emit(res) == "#-1 HTTP 301");
}

TEST_CASE("emit_result — HTTP 199 (below 2xx) produces '#-1 HTTP 199'", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 199;
    REQUIRE(run_emit(res) == "#-1 HTTP 199");
}

// ---------------------------------------------------------------------------
// emit_result — success path (2xx range)
// ---------------------------------------------------------------------------
TEST_CASE("emit_result — HTTP 200 returns body verbatim", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 200;
    res.body   = "{\"result\":\"ok\"}";
    REQUIRE(run_emit(res) == "{\"result\":\"ok\"}");
}

TEST_CASE("emit_result — HTTP 201 returns body", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 201;
    res.body   = "created";
    REQUIRE(run_emit(res) == "created");
}

TEST_CASE("emit_result — HTTP 204 with empty body returns empty string", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 204;
    res.body   = "";
    REQUIRE(run_emit(res).empty());
}

TEST_CASE("emit_result — HTTP 299 (upper 2xx boundary) returns body", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 299;
    res.body   = "ok";
    REQUIRE(run_emit(res) == "ok");
}

// ---------------------------------------------------------------------------
// emit_result — body truncation at HTTP_LBUF_SIZE-1
// ---------------------------------------------------------------------------
TEST_CASE("emit_result — body is capped at HTTP_LBUF_SIZE-1 bytes", "[httpclient][emit]")
{
    HttpResult res;
    res.ok     = true;
    res.status = 200;
    // Body larger than the output buffer
    res.body   = std::string(HTTP_LBUF_SIZE + 500, 'X');

    const std::string out = run_emit(res);
    // Must not overflow; output is at most HTTP_LBUF_SIZE-1 chars
    REQUIRE(out.size() <= HTTP_LBUF_SIZE - 1);
    // Must be all 'X'
    REQUIRE(out == std::string(out.size(), 'X'));
}

// ---------------------------------------------------------------------------
// curl_write_cb — accumulation and cap
// ---------------------------------------------------------------------------
TEST_CASE("curl_write_cb — appends bytes to std::string", "[httpclient][write_cb]")
{
    std::string buf;
    const char data[] = "hello";
    const std::size_t written = curl_write_cb(
        const_cast<char *>(data), 1, sizeof(data) - 1, &buf);
    REQUIRE(written == 5);
    REQUIRE(buf == "hello");
}

TEST_CASE("curl_write_cb — multiple calls accumulate", "[httpclient][write_cb]")
{
    std::string buf;
    const char part1[] = "foo";
    const char part2[] = "bar";
    curl_write_cb(const_cast<char *>(part1), 1, 3, &buf);
    curl_write_cb(const_cast<char *>(part2), 1, 3, &buf);
    REQUIRE(buf == "foobar");
}

TEST_CASE("curl_write_cb — returns total even when cap is exceeded", "[httpclient][write_cb]")
{
    std::string buf;
    // Pre-fill buf to just below the cap (2× HTTP_LBUF_SIZE)
    buf.assign(HTTP_LBUF_SIZE * 2 - 1, 'A');

    const char extra[] = "OVERFLOW";
    const std::size_t extra_len = sizeof(extra) - 1;

    // First byte puts us at cap; next call should be discarded
    const char one = 'B';
    curl_write_cb(const_cast<char *>(&one), 1, 1, &buf);  // fills to cap
    REQUIRE(buf.size() == HTTP_LBUF_SIZE * 2);

    // Now an overflow call — must still return extra_len (libcurl requires it)
    const std::size_t returned = curl_write_cb(
        const_cast<char *>(extra), 1, extra_len, &buf);
    REQUIRE(returned == extra_len);
    // Buffer must not have grown beyond the cap
    REQUIRE(buf.size() == HTTP_LBUF_SIZE * 2);
}

TEST_CASE("curl_write_cb — size×nmemb product is correct", "[httpclient][write_cb]")
{
    std::string buf;
    // Simulate a libcurl call with size=4, nmemb=3 (12 bytes total)
    const char data[12] = {'A','B','C','D','E','F','G','H','I','J','K','L'};
    const std::size_t written = curl_write_cb(
        const_cast<char *>(data), 4, 3, &buf);
    REQUIRE(written == 12);
    REQUIRE(buf.size() == 12);
}
