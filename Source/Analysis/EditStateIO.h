#pragma once
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

// EditState 전용 바이트 입출력 (리틀엔디언, 고정 폭). 내부용.
class ByteWriter
{
public:
    void u32(uint32_t v) { for (int i = 0; i < 4; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void u64(uint64_t v) { for (int i = 0; i < 8; ++i) bytes.push_back(static_cast<uint8_t>(v >> (8 * i))); }
    void i32(int32_t v) { u32(static_cast<uint32_t>(v)); }
    void boolean(bool v) { bytes.push_back(v ? 1 : 0); }
    void f64(double v) { uint64_t b; std::memcpy(&b, &v, 8); u64(b); }
    void f32(float v) { uint32_t b; std::memcpy(&b, &v, 4); u32(b); }
    void raw(const char* p, size_t n) { bytes.insert(bytes.end(), p, p + n); }
    void str(const std::string& s) { u32(static_cast<uint32_t>(s.size())); raw(s.data(), s.size()); }

    std::vector<uint8_t> bytes;
};

// 읽기 실패(범위 초과, 유한하지 않은 실수)는 ok=false로 남고 이후 읽기는 0을 돌려준다
class ByteReader
{
public:
    ByteReader(const uint8_t* d, size_t n) : data(d), size(n) {}

    uint32_t u32() { return static_cast<uint32_t>(le(4)); }
    uint64_t u64() { return le(8); }
    int32_t i32() { return static_cast<int32_t>(u32()); }
    bool boolean() { return le(1) != 0; }
    double f64() { const auto b = u64(); double v; std::memcpy(&v, &b, 8); return finite(v); }
    float f32() { const auto b = u32(); float v; std::memcpy(&v, &b, 4); return static_cast<float>(finite(v)); }
    size_t remaining() const noexcept { return size - pos; }
    bool atEnd() const noexcept { return pos == size; }

    // count개(각 elemBytes)를 읽을 만큼 남았는지 먼저 확인한다 (할당 전에 호출)
    bool fits(uint64_t count, size_t elemBytes)
    {
        if (ok && count <= remaining() / elemBytes)
            return true;
        ok = false;
        return false;
    }
    std::string str()
    {
        const auto n = u32();
        if (! fits(n, 1))
            return {};
        std::string s(reinterpret_cast<const char*>(data + pos), n);
        pos += n;
        return s;
    }

    bool ok = true;

private:
    uint64_t le(size_t n)
    {
        if (! ok || remaining() < n)
        {
            ok = false;
            return 0;
        }
        uint64_t v = 0;
        for (size_t i = 0; i < n; ++i)
            v |= static_cast<uint64_t>(data[pos + i]) << (8 * i);
        pos += n;
        return v;
    }
    double finite(double v)
    {
        if (! std::isfinite(v))
            ok = false;
        return ok ? v : 0.0;
    }

    const uint8_t* data;
    size_t size;
    size_t pos = 0;
};
