#pragma once

// Minimal JSON for settings.json: a single flat object of primitive values.
// Not a general parser — KelvinShift's settings file has no nesting or
// arrays. Tolerant on read (unknown keys ignored, missing keys -> caller
// default), and writes PascalCase keys so the file stays compatible with
// the settings.json produced by the earlier C#/.NET build.

#include <string>
#include <map>

class JsonReader
{
public:
    // Returns false only if the text isn't a JSON object at all.
    bool Parse(const std::string& text);

    bool Has(const std::string& key) const { return values_.count(key) != 0; }
    int GetInt(const std::string& key, int def) const;
    double GetDouble(const std::string& key, double def) const;
    bool GetBool(const std::string& key, bool def) const;
    std::string GetString(const std::string& key, const std::string& def) const;

private:
    // key -> raw scalar token (strings already unquoted + unescaped).
    std::map<std::string, std::string> values_;
};

class JsonWriter
{
public:
    void Int(const std::string& key, int v);
    void Double(const std::string& key, double v);
    void Bool(const std::string& key, bool v);
    void Str(const std::string& key, const std::string& v);
    std::string Done();

private:
    void Key(const std::string& key);
    std::string out_;
    bool first_ = true;
};
