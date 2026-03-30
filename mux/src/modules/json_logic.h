/*! \file json_logic.h
 * \brief JsonParse — minimal recursive-descent JSON parser and path navigator.
 *
 * This header contains the pure logic (no TinyMUX framework dependency) so it
 * can be included directly by the Catch2 test binary as well as by jsonparse.cpp.
 */
#ifndef JSON_LOGIC_H
#define JSON_LOGIC_H

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ===========================================================================
// JVal — tagged-union JSON value
// ===========================================================================

struct JVal
{
    enum class T { Null, Bool, Num, Str, Arr, Obj };
    T t = T::Null;
    bool b{};
    std::string s;                                     // Str content or raw Num text
    std::vector<JVal> a;                               // Array elements
    std::vector<std::pair<std::string, JVal>> o;       // Object key→value pairs
};

// ===========================================================================
// Internal parser helpers (all inline — no TU ownership issues in header)
// ===========================================================================

// Skip whitespace; return false if end-of-string reached.
inline bool json_skip_ws(std::string_view j, size_t &i)
{
    while (i < j.size() && (j[i]==' '||j[i]=='\t'||j[i]=='\n'||j[i]=='\r')) ++i;
    return i < j.size();
}

// Parse a JSON string starting at j[i] == '"'; advances i past the closing '"'.
inline std::optional<std::string> json_parse_str(std::string_view j, size_t &i)
{
    if (i >= j.size() || j[i] != '"') return {};
    ++i;
    std::string r;
    while (i < j.size() && j[i] != '"')
    {
        if (j[i] != '\\') { r += j[i++]; continue; }
        ++i;
        if (i >= j.size()) return {};
        switch (j[i])
        {
            case '"':  r += '"';  break;
            case '\\': r += '\\'; break;
            case '/':  r += '/';  break;
            case 'b':  r += '\b'; break;
            case 'f':  r += '\f'; break;
            case 'n':  r += '\n'; break;
            case 'r':  r += '\r'; break;
            case 't':  r += '\t'; break;
            case 'u':
            {
                if (i + 4 >= j.size()) return {};
                uint32_t cp = 0;
                for (int k = 1; k <= 4; ++k)
                {
                    const char c = j[i + static_cast<size_t>(k)];
                    uint32_t d;
                    if      (c>='0'&&c<='9') d = static_cast<uint32_t>(c-'0');
                    else if (c>='a'&&c<='f') d = static_cast<uint32_t>(c-'a'+10);
                    else if (c>='A'&&c<='F') d = static_cast<uint32_t>(c-'A'+10);
                    else return {};
                    cp = cp * 16 + d;
                }
                // Encode as UTF-8 (BMP only; surrogate pairs not supported)
                if      (cp < 0x80)  { r += static_cast<char>(cp); }
                else if (cp < 0x800) { r += static_cast<char>(0xC0|(cp>>6));
                                       r += static_cast<char>(0x80|(cp&0x3F)); }
                else                 { r += static_cast<char>(0xE0|(cp>>12));
                                       r += static_cast<char>(0x80|((cp>>6)&0x3F));
                                       r += static_cast<char>(0x80|(cp&0x3F)); }
                i += 4;
                break;
            }
            default: r += j[i]; break;
        }
        ++i;
    }
    if (i >= j.size()) return {};
    ++i;  // skip closing '"'
    return r;
}

// Parse a JSON value starting at j[i]; advances i past the value.
inline std::optional<JVal> json_parse_val(std::string_view j, size_t &i)
{
    if (!json_skip_ws(j, i)) return {};
    JVal v;

    // String
    if (j[i] == '"')
    {
        auto s = json_parse_str(j, i);
        if (!s) return {};
        v.t = JVal::T::Str;
        v.s = std::move(*s);
        return v;
    }

    // Object
    if (j[i] == '{')
    {
        ++i;
        v.t = JVal::T::Obj;
        if (!json_skip_ws(j, i)) return {};
        if (j[i] == '}') { ++i; return v; }
        for (;;)
        {
            if (!json_skip_ws(j, i)) return {};
            auto key = json_parse_str(j, i);
            if (!key) return {};
            if (!json_skip_ws(j, i) || j[i] != ':') return {};
            ++i;
            auto val = json_parse_val(j, i);
            if (!val) return {};
            v.o.emplace_back(std::move(*key), std::move(*val));
            if (!json_skip_ws(j, i)) return {};
            if (j[i] == '}') { ++i; return v; }
            if (j[i] != ',') return {};
            ++i;
        }
    }

    // Array
    if (j[i] == '[')
    {
        ++i;
        v.t = JVal::T::Arr;
        if (!json_skip_ws(j, i)) return {};
        if (j[i] == ']') { ++i; return v; }
        for (;;)
        {
            auto el = json_parse_val(j, i);
            if (!el) return {};
            v.a.push_back(std::move(*el));
            if (!json_skip_ws(j, i)) return {};
            if (j[i] == ']') { ++i; return v; }
            if (j[i] != ',') return {};
            ++i;
        }
    }

    // Literals: true / false / null
    auto starts = [&](const char *lit, size_t len) {
        return j.substr(i, len) == std::string_view(lit, len);
    };
    if (starts("true",  4)) { v.t = JVal::T::Bool; v.b = true;  i += 4; return v; }
    if (starts("false", 5)) { v.t = JVal::T::Bool; v.b = false; i += 5; return v; }
    if (starts("null",  4)) { v.t = JVal::T::Null;              i += 4; return v; }

    // Number
    if (j[i]=='-' || (j[i]>='0' && j[i]<='9'))
    {
        size_t start = i;
        if (j[i]=='-') ++i;
        while (i < j.size() && j[i]>='0' && j[i]<='9') ++i;
        if (i < j.size() && j[i]=='.') {
            ++i;
            while (i < j.size() && j[i]>='0' && j[i]<='9') ++i;
        }
        if (i < j.size() && (j[i]=='e'||j[i]=='E')) {
            ++i;
            if (i < j.size() && (j[i]=='+'||j[i]=='-')) ++i;
            while (i < j.size() && j[i]>='0' && j[i]<='9') ++i;
        }
        v.t = JVal::T::Num;
        v.s = std::string(j.substr(start, i - start));
        return v;
    }

    return {};  // parse error
}

// Parse a complete JSON document.
inline std::optional<JVal> parse_json(std::string_view s)
{
    size_t i = 0;
    return json_parse_val(s, i);
}

// ===========================================================================
// Path navigation
// ===========================================================================

// Tokenize "a.b[0].c" → ["a", "b", "[0]", "c"]
inline std::vector<std::string> tokenize_path(std::string_view path)
{
    std::vector<std::string> toks;
    std::string cur;
    for (const char c : path)
    {
        if (c == '.')
        {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
        }
        else if (c == '[')
        {
            if (!cur.empty()) { toks.push_back(cur); cur.clear(); }
            cur += c;
        }
        else if (c == ']')
        {
            cur += c;
            toks.push_back(cur); cur.clear();
        }
        else
        {
            cur += c;
        }
    }
    if (!cur.empty()) toks.push_back(cur);
    return toks;
}

// Navigate to the node at `path` relative to `root`.
// Returns nullptr if the path does not exist.
inline const JVal* navigate(const JVal &root, std::string_view path)
{
    if (path.empty()) return &root;
    const JVal *v = &root;
    for (const auto &tok : tokenize_path(path))
    {
        if (!v) return nullptr;
        if (tok.size() >= 3 && tok.front() == '[' && tok.back() == ']')
        {
            // Array index
            if (v->t != JVal::T::Arr) return nullptr;
            int idx = 0;
            try { idx = std::stoi(tok.substr(1, tok.size() - 2)); }
            catch (const std::exception &) { return nullptr; }
            if (idx < 0 || static_cast<size_t>(idx) >= v->a.size()) return nullptr;
            v = &v->a[static_cast<size_t>(idx)];
        }
        else
        {
            // Object key
            if (v->t != JVal::T::Obj) return nullptr;
            const JVal *found = nullptr;
            for (const auto &[k, val] : v->o)
                if (k == tok) { found = &val; break; }
            if (!found) return nullptr;
            v = found;
        }
    }
    return v;
}

// ===========================================================================
// Serialization
// ===========================================================================

// Append `s` to `out` as a JSON string literal (with surrounding quotes).
inline void json_escape_str(const std::string &s, std::string &out)
{
    out += '"';
    for (const unsigned char c : s)
    {
        switch (c)
        {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20)
                {
                    char buf[8];
                    std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                }
                else { out += static_cast<char>(c); }
                break;
        }
    }
    out += '"';
}

inline std::string serialize(const JVal &v)
{
    std::string out;
    switch (v.t)
    {
        case JVal::T::Null: out = "null";  break;
        case JVal::T::Bool: out = v.b ? "true" : "false"; break;
        case JVal::T::Num:  out = v.s;    break;
        case JVal::T::Str:  json_escape_str(v.s, out); break;
        case JVal::T::Arr:
            out += '[';
            for (size_t i = 0; i < v.a.size(); ++i)
            {
                if (i) out += ',';
                out += serialize(v.a[i]);
            }
            out += ']';
            break;
        case JVal::T::Obj:
            out += '{';
            for (size_t i = 0; i < v.o.size(); ++i)
            {
                if (i) out += ',';
                json_escape_str(v.o[i].first, out);
                out += ':';
                out += serialize(v.o[i].second);
            }
            out += '}';
            break;
    }
    return out;
}

#endif // JSON_LOGIC_H
