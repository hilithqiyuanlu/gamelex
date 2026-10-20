#pragma once
#include <array>
#include <cstddef>
#include <initializer_list>
#include <limits>
#include <string_view>

namespace gamelex::form {
// Views live until the synchronous transfer ends. Image bytes are converted
// directly to escaped Base64; no full-size intermediate string is allocated.
struct Field {
    std::string_view name, text;
    const unsigned char* bytes = nullptr;
    std::size_t size = 0;
    bool image = false;
    static Field jpeg(std::string_view name, const unsigned char* bytes, std::size_t size) {
        return {name, {}, bytes, size, true};
    }
};

class Stream {
public:
    static constexpr std::size_t MaxFields = 12;
    Stream(std::initializer_list<Field> values) {
        if (values.size() > MaxFields) { valid_ = false; return; }
        for (const auto& value : values) {
            if (value.image && ((!value.bytes && value.size) || value.size > (MaxSize / 4 - 1) * 3)) {
                valid_ = false; return;
            }
            fields_[count_++] = value;
        }
        for (std::size_t i = 0; i < count_ && valid_; ++i) {
            if (i) add(1);
            for (const auto ch : fields_[i].name) add(escaped_size(static_cast<unsigned char>(ch)));
            add(1);
            const auto& f = fields_[i];
            const auto size = value_size(f);
            for (std::size_t j = 0; j < size && valid_; ++j) add(escaped_size(value_byte(f, j)));
        }
    }
    bool valid() const { return valid_; }
    std::size_t length() const { return valid_ ? length_ : 0; }
    void rewind() { field_ = position_ = pending_at_ = pending_size_ = 0; part_ = Part::Separator; }
    std::size_t read(char* out, std::size_t capacity) {
        if (!valid_ || !out) return 0;
        std::size_t written = 0;
        while (written < capacity) {
            if (pending_at_ < pending_size_) {
                out[written++] = pending_[pending_at_++];
                continue;
            }
            unsigned char ch;
            bool escape;
            if (!next(ch, escape)) break;
            pending_at_ = 0;
            if (escape && !unreserved(ch)) {
                static constexpr char Hex[] = "0123456789ABCDEF";
                pending_ = {'%', Hex[ch >> 4], Hex[ch & 15]};
                pending_size_ = 3;
            } else {
                pending_[0] = static_cast<char>(ch); pending_size_ = 1;
            }
        }
        return written;
    }
private:
    static constexpr std::size_t MaxSize = std::numeric_limits<std::size_t>::max();
    enum class Part { Separator, Name, Equal, Value };
    std::array<Field, MaxFields> fields_{};
    std::array<char, 3> pending_{};
    std::size_t count_ = 0, length_ = 0, field_ = 0, position_ = 0, pending_at_ = 0, pending_size_ = 0;
    Part part_ = Part::Separator;
    bool valid_ = true;
    static bool unreserved(unsigned char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') ||
               (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
    }
    static std::size_t escaped_size(unsigned char ch) { return unreserved(ch) ? 1 : 3; }
    void add(std::size_t size) {
        if (size > MaxSize - length_) valid_ = false;
        else length_ += size;
    }
    static std::size_t value_size(const Field& f) { return f.image ? ((f.size + 2) / 3) * 4 : f.text.size(); }
    static unsigned char value_byte(const Field& f, std::size_t i) {
        if (!f.image) return static_cast<unsigned char>(f.text[i]);
        static constexpr char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        const auto base = (i / 4) * 3;
        const unsigned a = f.bytes[base];
        const unsigned b = base + 1 < f.size ? f.bytes[base + 1] : 0;
        const unsigned c = base + 2 < f.size ? f.bytes[base + 2] : 0;
        switch (i % 4) {
        case 0: return Alphabet[a >> 2];
        case 1: return Alphabet[((a & 3) << 4) | (b >> 4)];
        case 2: return base + 1 < f.size ? Alphabet[((b & 15) << 2) | (c >> 6)] : '=';
        default: return base + 2 < f.size ? Alphabet[c & 63] : '=';
        }
    }
    bool next(unsigned char& ch, bool& escape) {
        while (field_ < count_) {
            const auto& f = fields_[field_];
            escape = true;
            switch (part_) {
            case Part::Separator:
                part_ = Part::Name;
                if (field_) { ch = '&'; escape = false; return true; }
                break;
            case Part::Name:
                if (position_ < f.name.size()) { ch = f.name[position_++]; return true; }
                position_ = 0; part_ = Part::Equal; break;
            case Part::Equal:
                ch = '='; escape = false; part_ = Part::Value; return true;
            case Part::Value:
                if (position_ < value_size(f)) { ch = value_byte(f, position_++); return true; }
                position_ = 0; ++field_; part_ = Part::Separator; break;
            }
        }
        return false;
    }
};
}
