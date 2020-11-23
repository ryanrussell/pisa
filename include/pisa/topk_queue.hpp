#pragma once

#include <algorithm>

#include "util/compiler_attribute.hpp"
#include "util/likely.hpp"
#include "util/util.hpp"

namespace pisa {

/// How many `std::nextafter` is called when decreasing the initial threshold.
static constexpr std::size_t EPSILON_FACTOR = 10;

using Threshold = float;
struct topk_queue {
    using entry_type = std::pair<float, uint64_t>;

    explicit topk_queue(uint64_t k) : m_threshold(0), m_k(k) { m_q.reserve(m_k + 1); }
    topk_queue(topk_queue const&) = default;
    topk_queue(topk_queue&&) noexcept = default;
    topk_queue& operator=(topk_queue const&) = default;
    topk_queue& operator=(topk_queue&&) noexcept = default;
    ~topk_queue() = default;

    [[nodiscard]] constexpr static auto
    min_heap_order(entry_type const& lhs, entry_type const& rhs) noexcept -> bool
    {
        return lhs.first > rhs.first;
    }

    bool insert(float score) { return insert(score, 0); }

    bool insert(float score, uint64_t docid)
    {
        if (PISA_UNLIKELY(not would_enter(score))) {
            return false;
        }
        m_q.emplace_back(score, docid);
        if (PISA_UNLIKELY(m_q.size() <= m_k)) {
            std::push_heap(m_q.begin(), m_q.end(), min_heap_order);
            if (PISA_UNLIKELY(m_q.size() == m_k)) {
                m_threshold = m_q.front().first;
            }
        } else {
            std::pop_heap(m_q.begin(), m_q.end(), min_heap_order);
            m_q.pop_back();
            m_threshold = m_q.front().first;
        }
        return true;
    }

    PISA_ALWAYSINLINE bool would_enter(float score) const { return score > m_threshold; }

    void finalize()
    {
        m_threshold = size() == capacity() ? m_q.front().first : 0.0;
        std::sort_heap(m_q.begin(), m_q.end(), min_heap_order);
        size_t size = std::lower_bound(
                          m_q.begin(),
                          m_q.end(),
                          0,
                          [](std::pair<float, uint64_t> l, float r) { return l.first > r; })
            - m_q.begin();
        m_q.resize(size);
    }

    [[nodiscard]] std::vector<entry_type> const& topk() const noexcept { return m_q; }

    void set_threshold(Threshold t) noexcept
    {
        m_threshold = std::max(t - 0.0001, 0.0);
        /* for (int _i = 0; _i < EPSILON_FACTOR; ++_i) { */
        /*     t = std::nextafter(t, 0.0F); */
        /* } */
        /* m_threshold = t; */
    }

    void clear() noexcept
    {
        m_q.clear();
        m_threshold = 0;
    }

    [[nodiscard]] float threshold() const noexcept { return m_threshold; }

    [[nodiscard]] size_t capacity() const noexcept { return m_k; }

    [[nodiscard]] size_t size() const noexcept { return m_q.size(); }

  private:
    float m_threshold;
    uint64_t m_k;
    std::vector<entry_type> m_q;
};

}  // namespace pisa
