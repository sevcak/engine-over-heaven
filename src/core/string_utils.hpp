#pragma once

#include <core/types.hpp>
#include <string_view>

namespace StringUtils
{
/**
 * FNV-1a 32-bit hash function.
 *
 * @param s     String to hash.
 * @param count Length of the string.
 *
 * @returns 32-bit FNV-1a hash of the string.
 */
constexpr u32 fnvia_32(char const *s, usize count)
{
    // FNV offset basis.
    u32 hash = 2166136261u;

    for (usize i = 0; i < count; i++) {
        hash ^= static_cast<u32>(s[i]);
        //      FNV prime.
        hash *= 16777619u;
    }

    return hash;
}

constexpr usize const_strlen(const char *s)
{
    usize size = 0;
    while (s[size]) {
        size++;
    };
    return size;
}

struct StringHash
{
    u32 computed_hash;

    constexpr StringHash(u32 hash) noexcept : computed_hash { hash } {}

    constexpr StringHash(const char *s) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s, const_strlen(s));
    }

    constexpr StringHash(const char *s, usize count) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s, count);
    }

    constexpr StringHash(std::string_view s) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s.data(), s.size());
    }

    StringHash(const StringHash &other) = default;

    constexpr operator u32() const noexcept { return computed_hash; }

    constexpr bool operator==(const StringHash &other) const noexcept
    {
        return computed_hash == other.computed_hash;
    }
};

} // namespace StringUtils

template <>
struct std::hash<StringUtils::StringHash>
{
    usize operator()(const StringUtils::StringHash &k) const noexcept { return k.computed_hash; }
};