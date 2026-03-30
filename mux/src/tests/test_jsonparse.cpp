/*! \file test_jsonparse.cpp
 * \brief Catch2 unit tests for json_logic.h — JSON parser, navigator, and serializer.
 *
 * Tests cover parse_json(), navigate(), json_escape_str(), serialize(), and
 * the decode path (json_parse_str with surrounding quotes).
 */

#include <catch2/catch_all.hpp>
#include "../modules/json_logic.h"

// ---------------------------------------------------------------------------
// parse_json — value types
// ---------------------------------------------------------------------------
TEST_CASE("parse_json — null literal", "[json][parse]")
{
    const auto v = parse_json("null");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Null);
}

TEST_CASE("parse_json — boolean true", "[json][parse]")
{
    const auto v = parse_json("true");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Bool);
    REQUIRE(v->b == true);
}

TEST_CASE("parse_json — boolean false", "[json][parse]")
{
    const auto v = parse_json("false");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Bool);
    REQUIRE(v->b == false);
}

TEST_CASE("parse_json — integer number", "[json][parse]")
{
    const auto v = parse_json("42");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Num);
    REQUIRE(v->s == "42");
}

TEST_CASE("parse_json — negative number", "[json][parse]")
{
    const auto v = parse_json("-7");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Num);
    REQUIRE(v->s == "-7");
}

TEST_CASE("parse_json — floating point number", "[json][parse]")
{
    const auto v = parse_json("3.14");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Num);
    REQUIRE(v->s == "3.14");
}

TEST_CASE("parse_json — scientific notation number", "[json][parse]")
{
    const auto v = parse_json("1.5e10");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Num);
}

TEST_CASE("parse_json — simple string", "[json][parse]")
{
    const auto v = parse_json("\"hello\"");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Str);
    REQUIRE(v->s == "hello");
}

TEST_CASE("parse_json — empty string", "[json][parse]")
{
    const auto v = parse_json("\"\"");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Str);
    REQUIRE(v->s.empty());
}

TEST_CASE("parse_json — string with escape sequences", "[json][parse]")
{
    // \"  \\  \n  \t  \r
    const auto v = parse_json("\"line1\\nline2\\t\\\"quoted\\\"\"");
    REQUIRE(v.has_value());
    REQUIRE(v->s == "line1\nline2\t\"quoted\"");
}

TEST_CASE("parse_json — string with unicode escape \\u0041 = 'A'", "[json][parse]")
{
    const auto v = parse_json("\"\\u0041\"");
    REQUIRE(v.has_value());
    REQUIRE(v->s == "A");
}

TEST_CASE("parse_json — string with unicode escape \\u00E9 = é (UTF-8)", "[json][parse]")
{
    const auto v = parse_json("\"\\u00e9\"");
    REQUIRE(v.has_value());
    // U+00E9 = 0xC3 0xA9 in UTF-8
    REQUIRE(v->s.size() == 2);
    REQUIRE(static_cast<unsigned char>(v->s[0]) == 0xC3);
    REQUIRE(static_cast<unsigned char>(v->s[1]) == 0xA9);
}

TEST_CASE("parse_json — empty array", "[json][parse]")
{
    const auto v = parse_json("[]");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Arr);
    REQUIRE(v->a.empty());
}

TEST_CASE("parse_json — array of integers", "[json][parse]")
{
    const auto v = parse_json("[1,2,3]");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Arr);
    REQUIRE(v->a.size() == 3);
    REQUIRE(v->a[0].s == "1");
    REQUIRE(v->a[2].s == "3");
}

TEST_CASE("parse_json — array with mixed types", "[json][parse]")
{
    const auto v = parse_json("[1, \"two\", true, null]");
    REQUIRE(v.has_value());
    REQUIRE(v->a.size() == 4);
    REQUIRE(v->a[0].t == JVal::T::Num);
    REQUIRE(v->a[1].t == JVal::T::Str);
    REQUIRE(v->a[2].t == JVal::T::Bool);
    REQUIRE(v->a[3].t == JVal::T::Null);
}

TEST_CASE("parse_json — empty object", "[json][parse]")
{
    const auto v = parse_json("{}");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Obj);
    REQUIRE(v->o.empty());
}

TEST_CASE("parse_json — simple object", "[json][parse]")
{
    const auto v = parse_json("{\"hp\":100,\"mp\":50}");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Obj);
    REQUIRE(v->o.size() == 2);
    REQUIRE(v->o[0].first == "hp");
    REQUIRE(v->o[0].second.s == "100");
    REQUIRE(v->o[1].first == "mp");
}

TEST_CASE("parse_json — nested object", "[json][parse]")
{
    const auto v = parse_json("{\"vitals\":{\"hp\":75,\"alive\":true}}");
    REQUIRE(v.has_value());
    REQUIRE(v->o[0].first == "vitals");
    REQUIRE(v->o[0].second.t == JVal::T::Obj);
    REQUIRE(v->o[0].second.o[0].first == "hp");
}

TEST_CASE("parse_json — whitespace is ignored", "[json][parse]")
{
    const auto v = parse_json("  {  \"a\"  :  1  }  ");
    REQUIRE(v.has_value());
    REQUIRE(v->t == JVal::T::Obj);
    REQUIRE(v->o[0].first == "a");
}

// ---------------------------------------------------------------------------
// parse_json — malformed input
// ---------------------------------------------------------------------------
TEST_CASE("parse_json — empty input returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("").has_value());
}

TEST_CASE("parse_json — truncated string returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("\"unterminated").has_value());
}

TEST_CASE("parse_json — bad escape sequence returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("\"\\q\"").has_value());
}

TEST_CASE("parse_json — truncated object returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("{\"a\":1").has_value());
}

TEST_CASE("parse_json — object missing colon returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("{\"a\" 1}").has_value());
}

TEST_CASE("parse_json — bare word (not keyword) returns nullopt", "[json][parse][error]")
{
    REQUIRE_FALSE(parse_json("hello").has_value());
}

// ---------------------------------------------------------------------------
// navigate — path resolution
// ---------------------------------------------------------------------------

static JVal make_vitals()
{
    // {"name":"Hero","vitals":{"hp":100,"mp":50},"items":["sword","shield"]}
    return *parse_json(
        "{\"name\":\"Hero\","
        " \"vitals\":{\"hp\":100,\"mp\":50},"
        " \"items\":[\"sword\",\"shield\"]}");
}

TEST_CASE("navigate — empty path returns root", "[json][navigate]")
{
    const auto root = make_vitals();
    const auto *v = navigate(root, "");
    REQUIRE(v != nullptr);
    REQUIRE(v == &root);
}

TEST_CASE("navigate — top-level key", "[json][navigate]")
{
    const auto root = make_vitals();
    const auto *v = navigate(root, "name");
    REQUIRE(v != nullptr);
    REQUIRE(v->t == JVal::T::Str);
    REQUIRE(v->s == "Hero");
}

TEST_CASE("navigate — nested key (dot notation)", "[json][navigate]")
{
    const auto root = make_vitals();
    const auto *v = navigate(root, "vitals.hp");
    REQUIRE(v != nullptr);
    REQUIRE(v->t == JVal::T::Num);
    REQUIRE(v->s == "100");
}

TEST_CASE("navigate — array index", "[json][navigate]")
{
    const auto root = make_vitals();
    const auto *v = navigate(root, "items[0]");
    REQUIRE(v != nullptr);
    REQUIRE(v->s == "sword");
}

TEST_CASE("navigate — array index out of bounds returns nullptr", "[json][navigate]")
{
    const auto root = make_vitals();
    REQUIRE(navigate(root, "items[99]") == nullptr);
}

TEST_CASE("navigate — missing key returns nullptr", "[json][navigate]")
{
    const auto root = make_vitals();
    REQUIRE(navigate(root, "nonexistent") == nullptr);
}

TEST_CASE("navigate — indexing non-array returns nullptr", "[json][navigate]")
{
    const auto root = make_vitals();
    REQUIRE(navigate(root, "name[0]") == nullptr);
}

TEST_CASE("navigate — key on non-object returns nullptr", "[json][navigate]")
{
    const auto root = make_vitals();
    REQUIRE(navigate(root, "name.nested") == nullptr);
}

TEST_CASE("navigate — deeply nested path", "[json][navigate]")
{
    const auto root = *parse_json("{\"a\":{\"b\":{\"c\":\"deep\"}}}");
    const auto *v = navigate(root, "a.b.c");
    REQUIRE(v != nullptr);
    REQUIRE(v->s == "deep");
}

// ---------------------------------------------------------------------------
// json_escape_str — serialization of strings
// ---------------------------------------------------------------------------
TEST_CASE("json_escape_str — plain string gets surrounding quotes", "[json][escape]")
{
    std::string out;
    json_escape_str("hello", out);
    REQUIRE(out == "\"hello\"");
}

TEST_CASE("json_escape_str — double quote is escaped", "[json][escape]")
{
    std::string out;
    json_escape_str("say \"hi\"", out);
    REQUIRE(out == "\"say \\\"hi\\\"\"");
}

TEST_CASE("json_escape_str — backslash is escaped", "[json][escape]")
{
    std::string out;
    json_escape_str("a\\b", out);
    REQUIRE(out == "\"a\\\\b\"");
}

TEST_CASE("json_escape_str — newline is escaped", "[json][escape]")
{
    std::string out;
    json_escape_str("line1\nline2", out);
    REQUIRE(out == "\"line1\\nline2\"");
}

TEST_CASE("json_escape_str — tab is escaped", "[json][escape]")
{
    std::string out;
    json_escape_str("\t", out);
    REQUIRE(out == "\"\\t\"");
}

TEST_CASE("json_escape_str — control char below 0x20 becomes \\uXXXX", "[json][escape]")
{
    std::string out;
    json_escape_str("\x01", out);
    REQUIRE(out == "\"\\u0001\"");
}

TEST_CASE("json_escape_str — empty string gives empty JSON string", "[json][escape]")
{
    std::string out;
    json_escape_str("", out);
    REQUIRE(out == "\"\"");
}

// ---------------------------------------------------------------------------
// serialize — round-trip
// ---------------------------------------------------------------------------
TEST_CASE("serialize — null round-trip", "[json][serialize]")
{
    const auto v = parse_json("null");
    REQUIRE(serialize(*v) == "null");
}

TEST_CASE("serialize — boolean round-trip", "[json][serialize]")
{
    REQUIRE(serialize(*parse_json("true"))  == "true");
    REQUIRE(serialize(*parse_json("false")) == "false");
}

TEST_CASE("serialize — number round-trip", "[json][serialize]")
{
    REQUIRE(serialize(*parse_json("42"))   == "42");
    REQUIRE(serialize(*parse_json("-7"))   == "-7");
    REQUIRE(serialize(*parse_json("3.14")) == "3.14");
}

TEST_CASE("serialize — string round-trip preserves escaping", "[json][serialize]")
{
    // Parse JSON string with escape, serialize back — must round-trip
    const auto v = parse_json("\"line1\\nline2\"");
    REQUIRE(v.has_value());
    // v->s contains the actual newline after parsing
    const std::string serialized = serialize(*v);
    // Re-parse the serialized form — should equal original
    const auto v2 = parse_json(serialized);
    REQUIRE(v2.has_value());
    REQUIRE(v2->s == v->s);
}

TEST_CASE("serialize — empty array", "[json][serialize]")
{
    REQUIRE(serialize(*parse_json("[]")) == "[]");
}

TEST_CASE("serialize — array round-trip", "[json][serialize]")
{
    const auto v = parse_json("[1,2,3]");
    REQUIRE(serialize(*v) == "[1,2,3]");
}

TEST_CASE("serialize — empty object", "[json][serialize]")
{
    REQUIRE(serialize(*parse_json("{}")) == "{}");
}

TEST_CASE("serialize — object round-trip", "[json][serialize]")
{
    const auto v = parse_json("{\"hp\":100,\"mp\":50}");
    REQUIRE(serialize(*v) == "{\"hp\":100,\"mp\":50}");
}

// ---------------------------------------------------------------------------
// json_parse_str — decode path (JSONKEY_DECODE logic)
// ---------------------------------------------------------------------------
TEST_CASE("json_parse_str — decodes escaped quotes", "[json][decode]")
{
    const std::string_view sv = "\"say \\\"hello\\\"\"";
    size_t i = 0;
    const auto decoded = json_parse_str(sv, i);
    REQUIRE(decoded.has_value());
    REQUIRE(*decoded == "say \"hello\"");
}

TEST_CASE("json_parse_str — returns nullopt for non-string input", "[json][decode]")
{
    const std::string_view sv = "42";
    size_t i = 0;
    REQUIRE_FALSE(json_parse_str(sv, i).has_value());
}

TEST_CASE("json_parse_str — returns nullopt for unterminated string", "[json][decode]")
{
    const std::string_view sv = "\"unterminated";
    size_t i = 0;
    REQUIRE_FALSE(json_parse_str(sv, i).has_value());
}
