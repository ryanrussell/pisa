#pragma once

#include <cstdint>
#include <cstring>
#include <type_traits>
#include <utility>

#include <gsl/gsl_assert>
#include <gsl/span>
#include <tl/optional.hpp>

#include "util/likely.hpp"
#include "v1/bit_cast.hpp"
#include "v1/types.hpp"

namespace pisa::v1 {

template <typename Cursor>
[[nodiscard]] auto next(Cursor &&cursor) -> tl::optional<typename std::decay_t<Cursor>::value_type>
{
    cursor.advance();
    if (cursor.empty()) {
        return tl::nullopt;
    }
    return tl::make_optional(cursor.value());
}

/// Uncompressed example of implementation of a single value cursor.
template <typename T>
struct RawCursor {
    static_assert(std::is_trivially_copyable_v<T>);
    using value_type = T;

    /// Creates a cursor from the encoded bytes.
    explicit constexpr RawCursor(gsl::span<const std::byte> bytes) : m_bytes(bytes.subspan(4))
    {
        Expects(m_bytes.size() % sizeof(T) == 0);
        Expects(not m_bytes.empty());
    }

    /// Dereferences the current value.
    [[nodiscard]] constexpr auto operator*() const -> T
    {
        if (PISA_UNLIKELY(empty())) {
            return sentinel();
        }
        return bit_cast<T>(gsl::as_bytes(m_bytes.subspan(m_current, sizeof(T))));
    }

    /// Alias for `operator*()`.
    [[nodiscard]] constexpr auto value() const noexcept -> T { return *(*this); }

    /// Advances the cursor to the next position.
    constexpr void advance() { m_current += sizeof(T); }

    /// Moves the cursor to the position `pos`.
    constexpr void advance_to_position(std::size_t pos) { m_current = pos; }

    /// Moves the cursor to the next value equal or greater than `value`.
    constexpr void advance_to_geq(T value)
    {
        while (this->value() < value) {
            advance();
        }
    }

    /// Returns `true` if there is no elements left.
    [[nodiscard]] constexpr auto empty() const noexcept -> bool
    {
        return m_current == m_bytes.size();
    }

    /// Returns the current position.
    [[nodiscard]] constexpr auto position() const noexcept -> std::size_t { return m_current; }

    /// Returns the number of elements in the list.
    [[nodiscard]] constexpr auto size() const -> std::size_t { return m_bytes.size() / sizeof(T); }

    /// The sentinel value, such that `value() != nullopt` is equivalent to `*(*this) < sentinel()`.
    [[nodiscard]] constexpr auto sentinel() const -> T { return std::numeric_limits<T>::max(); }

   private:
    std::size_t m_current = 0;
    gsl::span<const std::byte> m_bytes;
};

template <typename T>
struct RawReader {
    static_assert(std::is_trivially_copyable<T>::value);
    using value_type = T;

    [[nodiscard]] auto read(gsl::span<const std::byte> bytes) const -> RawCursor<T>
    {
        return RawCursor<T>(bytes);
    }

    constexpr static auto encoding() -> std::uint32_t { return EncodingId::Raw; }
};

template <typename T>
struct RawWriter {
    static_assert(std::is_trivially_copyable<T>::value);
    using value_type = T;

    constexpr static auto encoding() -> std::uint32_t { return EncodingId::Raw; }

    void push(T const &posting) { m_postings.push_back(posting); }
    void push(T &&posting) { m_postings.push_back(posting); }

    [[nodiscard]] auto write(std::ostream &os) const -> std::size_t
    {
        assert(!m_postings.empty());
        std::uint32_t length = m_postings.size();
        os.write(reinterpret_cast<char const *>(&length), sizeof(length));
        auto memory = gsl::as_bytes(gsl::make_span(m_postings.data(), m_postings.size()));
        os.write(reinterpret_cast<char const *>(memory.data()), memory.size());
        return sizeof(length) + memory.size();
    }

    void reset() { m_postings.clear(); }

   private:
    std::vector<T> m_postings;
};

} // namespace pisa::v1
