#pragma once
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <map>
#include <variant>
#include <stdexcept>

// ============================================================
// Lightweight Protocol Buffers reader/writer for Swordigo saves
// Supports: varint, i32(float), i64(double), len(string/nested)
// No external protobuf library dependency.
// ============================================================

namespace proto {

// Wire types
enum WireType : uint8_t {
    WIRE_VARINT = 0,    // int32, int64, bool, enum
    WIRE_I64    = 1,    // double, fixed64
    WIRE_LEN    = 2,    // string, bytes, nested message
    WIRE_I32    = 5,    // float, fixed32
};

// A single protobuf field value
struct Field {
    uint32_t field_number;
    WireType wire_type;
    
    // Value storage (only one is valid based on wire_type)
    uint64_t    varint_val;     // WIRE_VARINT
    double      double_val;     // WIRE_I64
    float       float_val;      // WIRE_I32
    std::string bytes_val;      // WIRE_LEN (string or raw bytes of nested message)
    
    // Convenience accessors
    int64_t  as_int()    const { return static_cast<int64_t>(varint_val); }
    uint64_t as_uint()   const { return varint_val; }
    bool     as_bool()   const { return varint_val != 0; }
    double   as_double() const { return double_val; }
    float    as_float()  const { return float_val; }
    const std::string& as_string() const { return bytes_val; }
};

// ============================================================
// Decoder
// ============================================================
class Reader {
public:
    Reader(const uint8_t* data, size_t len) : data_(data), len_(len), pos_(0) {}
    Reader(const std::string& s) : data_((const uint8_t*)s.data()), len_(s.size()), pos_(0) {}

    bool has_data() const { return pos_ < len_; }
    size_t pos() const { return pos_; }
    size_t remaining() const { return len_ - pos_; }

    // Read next field (returns false if no more data)
    bool read_field(Field& f) {
        if (!has_data()) return false;
        
        uint64_t tag = read_varint();
        f.wire_type = static_cast<WireType>(tag & 0x07);
        f.field_number = static_cast<uint32_t>(tag >> 3);
        // Field number 0 is not a legal protobuf tag. Treating it as data used
        // to let a corrupt payload decode as a stream of unknown fields.
        if (f.field_number == 0)
            throw std::runtime_error("Protobuf: field number 0");
        f.varint_val = 0;
        f.double_val = 0;
        f.float_val = 0;
        f.bytes_val.clear();
        
        switch (f.wire_type) {
            case WIRE_VARINT:
                f.varint_val = read_varint();
                break;
            case WIRE_I64:
                f.double_val = read_double();
                break;
            case WIRE_I32:
                f.float_val = read_float();
                break;
            case WIRE_LEN: {
                uint64_t length = read_varint();
                if (length > len_ - pos_)
                    throw std::runtime_error("Protobuf: length exceeds buffer");
                f.bytes_val.assign((const char*)(data_ + pos_), static_cast<size_t>(length));
                pos_ += length;
                break;
            }
            default:
                throw std::runtime_error("Protobuf: unknown wire type " + std::to_string(f.wire_type));
        }
        return true;
    }

    // Read all fields into a vector
    std::vector<Field> read_all() {
        std::vector<Field> fields;
        Field f;
        while (read_field(f)) {
            fields.push_back(f);
        }
        return fields;
    }

    // Read all fields, grouped by field number (for repeated fields)
    std::map<uint32_t, std::vector<Field>> read_grouped() {
        std::map<uint32_t, std::vector<Field>> groups;
        Field f;
        while (read_field(f)) {
            groups[f.field_number].push_back(f);
        }
        return groups;
    }

private:
    const uint8_t* data_;
    size_t len_;
    size_t pos_;

    uint64_t read_varint() {
        uint64_t value = 0;
        int shift = 0;
        while (pos_ < len_) {
            uint8_t b = data_[pos_++];
            value |= (uint64_t)(b & 0x7F) << shift;
            if ((b & 0x80) == 0) return value;
            shift += 7;
            if (shift > 63) throw std::runtime_error("Protobuf: varint too long");
        }
        // A payload ending in a continuation byte used to return the partial
        // value silently, shifting every following field by one byte.
        throw std::runtime_error("Protobuf: truncated varint");
    }

    double read_double() {
        if (pos_ + 8 > len_) throw std::runtime_error("Protobuf: not enough data for double");
        double val;
        memcpy(&val, data_ + pos_, 8);
        pos_ += 8;
        return val;
    }

    float read_float() {
        if (pos_ + 4 > len_) throw std::runtime_error("Protobuf: not enough data for float");
        float val;
        memcpy(&val, data_ + pos_, 4);
        pos_ += 4;
        return val;
    }
};

// ============================================================
// Encoder
// ============================================================
class Writer {
public:
    void write_varint_field(uint32_t field_number, uint64_t value) {
        write_tag(field_number, WIRE_VARINT);
        write_varint(value);
    }

    void write_double_field(uint32_t field_number, double value) {
        write_tag(field_number, WIRE_I64);
        write_double(value);
    }

    void write_float_field(uint32_t field_number, float value) {
        write_tag(field_number, WIRE_I32);
        write_float(value);
    }

    void write_string_field(uint32_t field_number, const std::string& value) {
        write_tag(field_number, WIRE_LEN);
        write_varint(value.size());
        buf_.insert(buf_.end(), value.begin(), value.end());
    }

    void write_bytes_field(uint32_t field_number, const std::string& value) {
        write_string_field(field_number, value); // Same encoding
    }

    // Write a nested message (pass in pre-serialized bytes)
    void write_nested_field(uint32_t field_number, const Writer& nested) {
        write_tag(field_number, WIRE_LEN);
        write_varint(nested.buf_.size());
        buf_.insert(buf_.end(), nested.buf_.begin(), nested.buf_.end());
    }

    // Re-emit a field as-is (for fields we don't edit)
    void write_field(const Field& f) {
        switch (f.wire_type) {
            case WIRE_VARINT: write_varint_field(f.field_number, f.varint_val); break;
            case WIRE_I64:    write_double_field(f.field_number, f.double_val); break;
            case WIRE_I32:    write_float_field(f.field_number, f.float_val);   break;
            case WIRE_LEN:    write_bytes_field(f.field_number, f.bytes_val);   break;
        }
    }

    const std::vector<uint8_t>& data() const { return buf_; }
    std::string to_string() const { return std::string(buf_.begin(), buf_.end()); }
    size_t size() const { return buf_.size(); }
    void clear() { buf_.clear(); }

private:
    std::vector<uint8_t> buf_;

    void write_tag(uint32_t field_number, WireType wt) {
        write_varint((field_number << 3) | wt);
    }

    void write_varint(uint64_t value) {
        while (value > 0x7F) {
            buf_.push_back(static_cast<uint8_t>(value & 0x7F) | 0x80);
            value >>= 7;
        }
        buf_.push_back(static_cast<uint8_t>(value));
    }

    void write_double(double value) {
        uint8_t bytes[8];
        memcpy(bytes, &value, 8);
        buf_.insert(buf_.end(), bytes, bytes + 8);
    }

    void write_float(float value) {
        uint8_t bytes[4];
        memcpy(bytes, &value, 4);
        buf_.insert(buf_.end(), bytes, bytes + 4);
    }
};

} // namespace proto
