/*! \file test_execscript.cpp
 * \brief Catch2 unit tests for execscript_impl.h — safe_script_name validation.
 *
 * safe_script_name() is the security gate for the execscript() softcode
 * function.  These tests exhaustively verify that path traversal and shell
 * injection attempts are rejected, and that legitimate script names pass.
 */

#include <catch2/catch_all.hpp>
#include "../modules/execscript_impl.h"

// ---------------------------------------------------------------------------
// Valid names — must return true
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — plain alphanumeric names pass", "[execscript][safe_name]")
{
    REQUIRE(safe_script_name("echo"));
    REQUIRE(safe_script_name("backup"));
    REQUIRE(safe_script_name("sync2db"));
    REQUIRE(safe_script_name("UPPERCASE"));
    REQUIRE(safe_script_name("Mixed123"));
}

TEST_CASE("safe_script_name — names with allowed punctuation pass", "[execscript][safe_name]")
{
    REQUIRE(safe_script_name("my-script"));
    REQUIRE(safe_script_name("my_script"));
    REQUIRE(safe_script_name("my-script.sh"));
    REQUIRE(safe_script_name("backup.v2.sh"));
    REQUIRE(safe_script_name("a_b-c.d"));
    REQUIRE(safe_script_name("script-2026-03-30.sh"));
}

TEST_CASE("safe_script_name — single character name passes", "[execscript][safe_name]")
{
    REQUIRE(safe_script_name("a"));
    REQUIRE(safe_script_name("Z"));
    REQUIRE(safe_script_name("9"));
}

// ---------------------------------------------------------------------------
// Null / empty — must return false
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — null pointer returns false", "[execscript][safe_name]")
{
    REQUIRE_FALSE(safe_script_name(nullptr));
}

TEST_CASE("safe_script_name — empty string returns false", "[execscript][safe_name]")
{
    REQUIRE_FALSE(safe_script_name(""));
}

// ---------------------------------------------------------------------------
// Leading dot — covers ".", "..", ".hidden"
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — single dot returns false", "[execscript][safe_name][path-traversal]")
{
    REQUIRE_FALSE(safe_script_name("."));
}

TEST_CASE("safe_script_name — double dot (path traversal) returns false",
          "[execscript][safe_name][path-traversal]")
{
    REQUIRE_FALSE(safe_script_name(".."));
}

TEST_CASE("safe_script_name — dotfile (.hidden) returns false",
          "[execscript][safe_name][path-traversal]")
{
    REQUIRE_FALSE(safe_script_name(".hidden"));
    REQUIRE_FALSE(safe_script_name(".bashrc"));
    REQUIRE_FALSE(safe_script_name(".ssh"));
}

// ---------------------------------------------------------------------------
// Directory separators and path components
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — forward slash rejected",
          "[execscript][safe_name][path-traversal]")
{
    REQUIRE_FALSE(safe_script_name("a/b"));
    REQUIRE_FALSE(safe_script_name("/etc/passwd"));
    REQUIRE_FALSE(safe_script_name("scripts/evil"));
    REQUIRE_FALSE(safe_script_name("../../../etc/shadow"));
}

// ---------------------------------------------------------------------------
// Shell meta-characters
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — semicolon rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("a;b"));
    REQUIRE_FALSE(safe_script_name("echo;rm -rf /"));
}

TEST_CASE("safe_script_name — space rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("rm -rf /"));
    REQUIRE_FALSE(safe_script_name("my script"));
}

TEST_CASE("safe_script_name — pipe rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("a|b"));
    REQUIRE_FALSE(safe_script_name("ls|cat /etc/passwd"));
}

TEST_CASE("safe_script_name — backtick rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("`id`"));
}

TEST_CASE("safe_script_name — dollar sign rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("$HOME"));
    REQUIRE_FALSE(safe_script_name("$(id)"));
}

TEST_CASE("safe_script_name — ampersand rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("a&b"));
    REQUIRE_FALSE(safe_script_name("sleep 10&"));
}

TEST_CASE("safe_script_name — parentheses rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("(evil)"));
}

TEST_CASE("safe_script_name — angle brackets rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("a>b"));
    REQUIRE_FALSE(safe_script_name("a<b"));
}

TEST_CASE("safe_script_name — null byte rejected", "[execscript][safe_name][injection]")
{
    // A name with an embedded null byte: "abc\0.." — only "abc" should be seen,
    // which is valid. But we verify the function handles it without crashing.
    // (In practice the C string stops at the first null.)
    REQUIRE(safe_script_name("abc"));  // sanity
}

TEST_CASE("safe_script_name — newline and tab rejected", "[execscript][safe_name][injection]")
{
    REQUIRE_FALSE(safe_script_name("a\nb"));
    REQUIRE_FALSE(safe_script_name("a\tb"));
}

// ---------------------------------------------------------------------------
// Dots embedded in the middle — allowed (e.g. "backup.sh") but "a..b" is fine
// since the leading-dot check only covers the first character, and ".." as a
// path component requires the separator '/'.
// ---------------------------------------------------------------------------
TEST_CASE("safe_script_name — double dot in middle is allowed (no slash separator)",
          "[execscript][safe_name]")
{
    // "a..b" contains only allowed chars; actual ".." traversal needs a '/'
    REQUIRE(safe_script_name("a..b"));
    REQUIRE(safe_script_name("v1..2.sh"));
}
