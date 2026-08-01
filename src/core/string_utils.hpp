#include <cstddef>
#include <cstdint>
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
constexpr uint32_t fnvia_32(char const *s, std::size_t count)
{
    // FNV offset basis.
    uint32_t hash = 2166136261u;

    for (std::size_t i = 0; i < count; i++) {
        hash ^= static_cast<uint32_t>(s[i]);
        //      FNV prime.
        hash *= 16777619u;
    }

    return hash;
}

constexpr std::size_t const_strlen(const char *s)
{
    size_t size = 0;
    while (s[size]) {
        size++;
    };
    return size;
}

struct StringHash
{
    uint32_t computed_hash;

    constexpr StringHash(uint32_t hash) noexcept : computed_hash { hash } {}

    constexpr StringHash(const char *s) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s, const_strlen(s));
    }

    constexpr StringHash(const char *s, std::size_t count) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s, count);
    }

    constexpr StringHash(std::string_view s) noexcept : computed_hash { 0 }
    {
        computed_hash = fnvia_32(s.data(), s.size());
    }

    StringHash(const StringHash &other) = default;

    constexpr operator uint32_t() const noexcept { return computed_hash; }
};

} // namespace StringUtils