#pragma once

#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace navidrome {

namespace json {

class Parser;

class Value {
public:
    enum Type { Null, Bool, Number, String, Array, Object };

    Value() = default;

    Type type() const { return type_; }
    bool isNull()   const { return type_ == Null; }
    bool isObject() const { return type_ == Object; }
    bool isArray()  const { return type_ == Array; }

    bool   asBool(bool def = false)   const { return type_ == Bool   ? b_ : def; }
    double asNumber(double def = 0.0) const { return type_ == Number ? n_ : def; }
    const std::string& asString() const {
        static const std::string kEmpty;
        return type_ == String ? s_ : kEmpty;
    }

    bool has(const std::string& key) const {
        if (type_ != Object) return false;
        for (const auto& kv : obj_) if (kv.first == key) return true;
        return false;
    }

    const Value& operator[](const std::string& key) const {
        if (type_ == Object)
            for (const auto& kv : obj_)
                if (kv.first == key) return kv.second;
        return nullRef();
    }
    const Value& operator[](const char* key) const {
        return (*this)[std::string(key)];
    }

    std::size_t size() const {
        return type_ == Array ? arr_.size() : (type_ == Object ? obj_.size() : 0);
    }
    const Value& operator[](std::size_t i) const {
        return (type_ == Array && i < arr_.size()) ? arr_[i] : nullRef();
    }

    std::vector<const Value*> items() const {
        std::vector<const Value*> out;
        if (type_ == Array) {
            out.reserve(arr_.size());
            for (const auto& e : arr_) out.push_back(&e);
        } else if (type_ == Object) {
            out.push_back(this);
        }
        return out;
    }

private:
    static const Value& nullRef() { static const Value kNull; return kNull; }

    Type type_ = Null;
    bool b_ = false;
    double n_ = 0.0;
    std::string s_;
    std::vector<Value> arr_;
    std::vector<std::pair<std::string, Value>> obj_;

    friend class Parser;
};

class Parser {
public:
    static Value parse(const std::string& text, std::string& err) {
        Parser p(text);
        p.ws();
        Value v = p.value();
        if (!p.err_.empty()) { err = p.err_; return Value(); }
        p.ws();
        if (p.i_ != p.text_.size()) { err = "trailing content after JSON value"; return Value(); }
        err.clear();
        return v;
    }

private:
    explicit Parser(const std::string& t) : text_(t) {}

    const std::string& text_;
    std::size_t i_ = 0;
    std::string err_;

    void fail(const char* m) { if (err_.empty()) err_ = m; }
    bool bad() const { return !err_.empty(); }

    void ws() {
        while (i_ < text_.size()) {
            char c = text_[i_];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++i_;
            else break;
        }
    }

    Value value() {
        if (bad()) return Value();
        if (i_ >= text_.size()) { fail("unexpected end of JSON"); return Value(); }
        switch (text_[i_]) {
            case '{': return object();
            case '[': return array();
            case '"': { Value v; v.type_ = Value::String; v.s_ = str(); return v; }
            case 't': case 'f': return boolean();
            case 'n': return null();
            default:  return number();
        }
    }

    Value object() {
        Value v; v.type_ = Value::Object;
        ++i_; ws();
        if (i_ < text_.size() && text_[i_] == '}') { ++i_; return v; }
        for (;;) {
            ws();
            if (i_ >= text_.size() || text_[i_] != '"') { fail("expected string key"); return Value(); }
            std::string key = str();
            if (bad()) return Value();
            ws();
            if (i_ >= text_.size() || text_[i_] != ':') { fail("expected ':'"); return Value(); }
            ++i_; ws();
            Value child = value();
            if (bad()) return Value();
            v.obj_.emplace_back(std::move(key), std::move(child));
            ws();
            if (i_ >= text_.size()) { fail("unterminated object"); return Value(); }
            if (text_[i_] == ',') { ++i_; continue; }
            if (text_[i_] == '}') { ++i_; break; }
            fail("expected ',' or '}'"); return Value();
        }
        return v;
    }

    Value array() {
        Value v; v.type_ = Value::Array;
        ++i_; ws();
        if (i_ < text_.size() && text_[i_] == ']') { ++i_; return v; }
        for (;;) {
            ws();
            Value child = value();
            if (bad()) return Value();
            v.arr_.push_back(std::move(child));
            ws();
            if (i_ >= text_.size()) { fail("unterminated array"); return Value(); }
            if (text_[i_] == ',') { ++i_; continue; }
            if (text_[i_] == ']') { ++i_; break; }
            fail("expected ',' or ']'"); return Value();
        }
        return v;
    }

    Value boolean() {
        if (text_.compare(i_, 4, "true") == 0)  { i_ += 4; Value v; v.type_ = Value::Bool; v.b_ = true;  return v; }
        if (text_.compare(i_, 5, "false") == 0) { i_ += 5; Value v; v.type_ = Value::Bool; v.b_ = false; return v; }
        fail("invalid literal"); return Value();
    }

    Value null() {
        if (text_.compare(i_, 4, "null") == 0) { i_ += 4; return Value(); }
        fail("invalid literal"); return Value();
    }

    Value number() {
        std::size_t start = i_;
        if (i_ < text_.size() && (text_[i_] == '-' || text_[i_] == '+')) ++i_;
        bool any = false;
        while (i_ < text_.size()) {
            char c = text_[i_];
            if ((c >= '0' && c <= '9') || c == '.' || c == 'e' || c == 'E' ||
                c == '+' || c == '-') { any = true; ++i_; }
            else break;
        }
        if (!any) { fail("invalid value"); return Value(); }
        Value v; v.type_ = Value::Number;
        v.n_ = std::strtod(text_.c_str() + start, nullptr);
        return v;
    }

    std::string str() {
        std::string out;
        ++i_;
        while (i_ < text_.size()) {
            char c = text_[i_++];
            if (c == '"') return out;
            if (c != '\\') { out.push_back(c); continue; }
            if (i_ >= text_.size()) break;
            char e = text_[i_++];
            switch (e) {
                case '"':  out.push_back('"');  break;
                case '\\': out.push_back('\\'); break;
                case '/':  out.push_back('/');  break;
                case 'b':  out.push_back('\b'); break;
                case 'f':  out.push_back('\f'); break;
                case 'n':  out.push_back('\n'); break;
                case 'r':  out.push_back('\r'); break;
                case 't':  out.push_back('\t'); break;
                case 'u': {
                    unsigned cp = hex4();
                    if (cp >= 0xD800 && cp <= 0xDBFF &&
                        i_ + 1 < text_.size() && text_[i_] == '\\' && text_[i_ + 1] == 'u') {
                        i_ += 2;
                        unsigned lo = hex4();
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                    }
                    appendUtf8(out, cp);
                    break;
                }
                default: out.push_back(e); break;
            }
        }
        fail("unterminated string");
        return out;
    }

    unsigned hex4() {
        unsigned v = 0;
        for (int k = 0; k < 4 && i_ < text_.size(); ++k) {
            char c = text_[i_++];
            v <<= 4;
            if      (c >= '0' && c <= '9') v |= unsigned(c - '0');
            else if (c >= 'a' && c <= 'f') v |= unsigned(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') v |= unsigned(c - 'A' + 10);
            else { fail("invalid \\u escape"); return v; }
        }
        return v;
    }

    static void appendUtf8(std::string& out, unsigned cp) {
        if (cp <= 0x7F) {
            out.push_back(char(cp));
        } else if (cp <= 0x7FF) {
            out.push_back(char(0xC0 | (cp >> 6)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else if (cp <= 0xFFFF) {
            out.push_back(char(0xE0 | (cp >> 12)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        } else {
            out.push_back(char(0xF0 | (cp >> 18)));
            out.push_back(char(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(char(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(char(0x80 | (cp & 0x3F)));
        }
    }
};

inline Value parse(const std::string& text, std::string& err) {
    return Parser::parse(text, err);
}
}
}
