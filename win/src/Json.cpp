#include "Json.h"
#include <cstdlib>
#include <cstdio>
#include <cmath>

namespace {

void SkipWs(const std::string& s, size_t& i)
{
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n'))
        ++i;
}

// Reads a JSON string starting at the opening quote; returns the unescaped
// contents and advances i past the closing quote.
bool ReadString(const std::string& s, size_t& i, std::string& out)
{
    if (i >= s.size() || s[i] != '"') return false;
    ++i;
    out.clear();
    while (i < s.size())
    {
        char c = s[i++];
        if (c == '"') return true;
        if (c == '\\' && i < s.size())
        {
            char e = s[i++];
            switch (e)
            {
                case '"':  out += '"';  break;
                case '\\': out += '\\'; break;
                case '/':  out += '/';  break;
                case 'n':  out += '\n'; break;
                case 't':  out += '\t'; break;
                case 'r':  out += '\r'; break;
                case 'b':  out += '\b'; break;
                case 'f':  out += '\f'; break;
                case 'u':
                {
                    if (i + 4 > s.size()) return false;
                    unsigned cp = (unsigned)strtoul(s.substr(i, 4).c_str(), nullptr, 16);
                    i += 4;
                    // Encode the BMP code point as UTF-8 (settings strings are
                    // plain ASCII/Latin in practice; no surrogate handling).
                    if (cp < 0x80) out += (char)cp;
                    else if (cp < 0x800)
                    {
                        out += (char)(0xC0 | (cp >> 6));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    else
                    {
                        out += (char)(0xE0 | (cp >> 12));
                        out += (char)(0x80 | ((cp >> 6) & 0x3F));
                        out += (char)(0x80 | (cp & 0x3F));
                    }
                    break;
                }
                default: out += e; break;
            }
        }
        else
        {
            out += c;
        }
    }
    return false;
}

// Reads a bare scalar (number / true / false / null) into raw text.
void ReadScalar(const std::string& s, size_t& i, std::string& out)
{
    size_t start = i;
    while (i < s.size())
    {
        char c = s[i];
        if (c == ',' || c == '}' || c == ' ' || c == '\t' || c == '\r' || c == '\n')
            break;
        ++i;
    }
    out = s.substr(start, i - start);
}

} // namespace

bool JsonReader::Parse(const std::string& text)
{
    values_.clear();
    size_t i = 0;
    SkipWs(text, i);
    if (i >= text.size() || text[i] != '{') return false;
    ++i;
    while (true)
    {
        SkipWs(text, i);
        if (i >= text.size()) return false;
        if (text[i] == '}') return true;
        if (text[i] == ',') { ++i; continue; }

        std::string key;
        if (!ReadString(text, i, key)) return false;
        SkipWs(text, i);
        if (i >= text.size() || text[i] != ':') return false;
        ++i;
        SkipWs(text, i);
        if (i >= text.size()) return false;

        std::string value;
        if (text[i] == '"')
        {
            if (!ReadString(text, i, value)) return false;
        }
        else
        {
            ReadScalar(text, i, value);
        }
        values_[key] = value;
    }
}

int JsonReader::GetInt(const std::string& key, int def) const
{
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return def;
    return (int)llround(strtod(it->second.c_str(), nullptr));
}

double JsonReader::GetDouble(const std::string& key, double def) const
{
    auto it = values_.find(key);
    if (it == values_.end() || it->second.empty()) return def;
    return strtod(it->second.c_str(), nullptr);
}

bool JsonReader::GetBool(const std::string& key, bool def) const
{
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    return it->second == "true";
}

std::string JsonReader::GetString(const std::string& key, const std::string& def) const
{
    auto it = values_.find(key);
    if (it == values_.end()) return def;
    return it->second;
}

// ── Writer ────────────────────────────────────────────────────────────────

void JsonWriter::Key(const std::string& key)
{
    if (!first_) out_ += ",\n";
    first_ = false;
    out_ += "  \"";
    out_ += key;
    out_ += "\": ";
}

void JsonWriter::Int(const std::string& key, int v)
{
    Key(key);
    out_ += std::to_string(v);
}

void JsonWriter::Double(const std::string& key, double v)
{
    Key(key);
    // %.10g keeps brightness/lat/lon compact and round-trips cleanly; matches
    // the way System.Text.Json renders doubles (1, 0.8, ...).
    char buf[64];
    snprintf(buf, sizeof(buf), "%.10g", v);
    out_ += buf;
}

void JsonWriter::Bool(const std::string& key, bool v)
{
    Key(key);
    out_ += v ? "true" : "false";
}

void JsonWriter::Str(const std::string& key, const std::string& v)
{
    Key(key);
    out_ += '"';
    for (char c : v)
    {
        switch (c)
        {
            case '"':  out_ += "\\\""; break;
            case '\\': out_ += "\\\\"; break;
            case '\n': out_ += "\\n";  break;
            case '\r': out_ += "\\r";  break;
            case '\t': out_ += "\\t";  break;
            default:   out_ += c;      break;
        }
    }
    out_ += '"';
}

std::string JsonWriter::Done()
{
    return "{\n" + out_ + "\n}\n";
}
