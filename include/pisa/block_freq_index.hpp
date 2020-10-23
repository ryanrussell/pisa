#pragma once

#include <condition_variable>
#include <mutex>
#include <thread>

#include "bit_vector.hpp"
#include "mappable/mappable_vector.hpp"
#include "mappable/mapper.hpp"

#include "block_posting_list.hpp"
#include "codec/compact_elias_fano.hpp"
#include "mappable/mapper.hpp"
#include "memory_source.hpp"
#include "temporary_directory.hpp"

namespace pisa {

struct BlockIndexTag;

template <typename BlockCodec, bool Profile = false, IndexArity Arity = IndexArity::Unary>
class block_freq_index {
  public:
    using index_layout_tag = BlockIndexTag;
    block_freq_index() = default;
    explicit block_freq_index(MemorySource source) : m_source(std::move(source))
    {
        auto freeze_flags = bit_cast<std::uint64_t>(m_source.subspan(0, 8).span());
        std::size_t offset = 8;
        constexpr auto param_size = sizeof(global_parameters);

        auto param_mem = m_source.subspan(offset, param_size);
        m_params = bit_cast<global_parameters>(param_mem.span());
        offset += param_mem.size();

        auto sizes_mem = m_source.subspan(offset, 32);
        m_size = bit_cast<std::size_t>(sizes_mem.subspan(0, 8));
        m_num_docs = bit_cast<std::size_t>(sizes_mem.subspan(8, 8));
        auto endpoint_size = bit_cast<std::size_t>(sizes_mem.subspan(24, 8));
        offset += 16;

        m_endpoint_memory = m_source.subspan(offset, 8 * endpoint_size + 16);
        auto mapper = mapper::detail::map_visitor(m_endpoint_memory.data(), 0, freeze_flags);
        m_endpoints.map(mapper);

        m_list_offset = offset + m_endpoint_memory.size() + 8;
        m_list_size = m_source.size() - m_list_offset;
    }

    class builder {
      public:
        builder(uint64_t num_docs, global_parameters const& params) : m_params(params)
        {
            m_num_docs = num_docs;
            m_endpoints.push_back(0);
        }

        template <typename DocsIterator, typename FreqsIterator>
        void add_posting_list(
            uint64_t n,
            DocsIterator docs_begin,
            FreqsIterator freqs_begin,
            uint64_t /* occurrences */)
        {
            if (!n) {
                throw std::invalid_argument("List must be nonempty");
            }
            block_posting_list<BlockCodec, Profile, Arity>::write(
                m_lists, n, docs_begin, freqs_begin);
            m_endpoints.push_back(m_lists.size());
        }

        template <typename BlockDataRange>
        void add_posting_list(uint64_t n, BlockDataRange const& blocks)
        {
            if (!n) {
                throw std::invalid_argument("List must be nonempty");
            }
            block_posting_list<BlockCodec>::write_blocks(m_lists, n, blocks);
            m_endpoints.push_back(m_lists.size());
        }

        template <typename BytesRange>
        void add_posting_list(BytesRange const& data)
        {
            m_lists.insert(m_lists.end(), std::begin(data), std::end(data));
            m_endpoints.push_back(m_lists.size());
        }

        void build(block_freq_index& sq)
        {
            sq.m_params = m_params;
            sq.m_size = m_endpoints.size() - 1;
            sq.m_num_docs = m_num_docs;
            sq.m_source = MemorySource::from_vector(std::move(m_lists));
            sq.m_list_offset = 0;
            sq.m_list_size = sq.m_source.size();

            bit_vector_builder bvb;
            compact_elias_fano::write(
                bvb,
                m_endpoints.begin(),
                sq.m_list_size,
                sq.m_size,
                m_params);  // XXX
            bit_vector(&bvb).swap(sq.m_endpoints);
        }

      private:
        global_parameters m_params;
        size_t m_num_docs;
        std::vector<uint64_t> m_endpoints;
        std::vector<std::uint8_t> m_lists;
    };

    class stream_builder {
        static constexpr std::size_t buffer_size = 1ULL << 30;

      public:
        stream_builder(uint64_t num_docs, global_parameters const& params)
            : m_params(params), m_postings_output((m_tmp.path() / "buffer").c_str())
        {
            m_num_docs = num_docs;
            m_endpoints.push_back(0);
            m_buffer.reserve(buffer_size);
        }

        void flush()
        {
            m_postings_output.write(reinterpret_cast<char const*>(m_buffer.data()), m_buffer.size());
            m_buffer.clear();
        }

        void maybe_flush()
        {
            if (m_buffer.size() >= buffer_size) {
                flush();
            }
        }

        template <typename DocsIterator, typename FreqsIterator>
        void add_posting_list(
            uint64_t n,
            DocsIterator docs_begin,
            FreqsIterator freqs_begin,
            uint64_t /* occurrences */)
        {
            if (!n) {
                throw std::invalid_argument("List must be nonempty");
            }
            auto old_size = m_buffer.size();
            block_posting_list<BlockCodec, Profile, Arity>::write(
                m_buffer, n, docs_begin, freqs_begin);
            m_postings_bytes_written += m_buffer.size() - old_size;
            m_endpoints.push_back(m_postings_bytes_written);
            maybe_flush();
        }

        void build(std::string const& index_path)
        {
            flush();

            std::ofstream os(index_path.c_str());
            mapper::detail::freeze_visitor freezer(os, 0);
            freezer(m_params, "m_params");
            std::size_t size = m_endpoints.size() - 1;
            freezer(size, "size");
            freezer(m_num_docs, "m_num_docs");

            bit_vector_builder bvb;
            compact_elias_fano::write(
                bvb, m_endpoints.begin(), m_postings_bytes_written, size, m_params);
            bit_vector endpoints(&bvb);
            freezer(endpoints, "endpoints");

            std::ifstream buf((m_tmp.path() / "buffer").c_str());
            m_postings_output.close();
            os.write(
                reinterpret_cast<char const*>(&m_postings_bytes_written),
                sizeof(m_postings_bytes_written));
            os << buf.rdbuf();
        }

        template <typename Iter>
        static void merge_into(Iter first, Iter last, std::string const& index_path)
        {
            std::ofstream os(index_path.c_str());
            mapper::detail::freeze_visitor freezer(os, 0);
            freezer(first->m_params, "m_params");
            std::size_t size = std::accumulate(first, last, 0, [](auto size, auto&& builder) {
                return size + builder.m_endpoints.size() - 1;
            });
            freezer(size, "size");
            freezer(first->m_num_docs, "m_num_docs");

            std::vector<uint64_t> endpoints(size + 1);
            std::size_t position_offset = 0;
            std::size_t endpoint_offset = 0;
            for (auto iter = first; iter != last; ++iter) {
                auto pos = std::transform(
                    iter->m_endpoints.begin(),
                    iter->m_endpoints.end(),
                    std::next(endpoints.begin(), position_offset),
                    [endpoint_offset](auto endpoint) { return endpoint + endpoint_offset; });
                position_offset = std::distance(endpoints.begin(), pos) - 1;
                endpoint_offset = endpoints[position_offset];
                iter->m_endpoints.clear();
            }

            std::size_t postings_bytes =
                std::accumulate(first, last, 0, [](auto acc, auto&& builder) {
                    return acc + builder.m_postings_bytes_written;
                });

            bit_vector_builder bvb;
            compact_elias_fano::write(bvb, endpoints.begin(), postings_bytes, size, first->m_params);
            bit_vector endpoints_bit_vector(&bvb);
            freezer(endpoints_bit_vector, "endpoints");

            os.write(reinterpret_cast<char const*>(&postings_bytes), sizeof(postings_bytes));

            for (auto iter = first; iter != last; ++iter) {
                iter->m_postings_output.close();
                std::ifstream buf((iter->tmp.path() / "buffer").c_str());
                os << buf.rdbuf();
            }
        }

      private:
        global_parameters m_params{};
        size_t m_num_docs = 0;
        size_t m_size = 0;
        std::vector<uint64_t> m_endpoints{};
        TemporaryDirectory m_tmp{};
        std::ofstream m_postings_output;
        std::size_t m_postings_bytes_written{0};
        std::vector<std::uint8_t> m_buffer;
    };

    size_t size() const { return m_size; }

    uint64_t num_docs() const { return m_num_docs; }

    using document_enumerator =
        typename block_posting_list<BlockCodec, Profile, Arity>::document_enumerator;

    document_enumerator operator[](size_t i) const
    {
        assert(i < size());
        compact_elias_fano::enumerator endpoints(m_endpoints, 0, m_list_size, m_size, m_params);

        auto startpoint = endpoints.move(i).second;
        auto length =
            i + 1 < size() ? endpoints.move(i + 1).second - startpoint : gsl::dynamic_extent;
        assert(length > 0);
        return document_enumerator(
            m_source.subspan(m_list_offset + startpoint, length), num_docs(), i);
    }

    void warmup(size_t i) const
    {
        assert(i < size());
        compact_elias_fano::enumerator endpoints(m_endpoints, 0, m_list_size, m_size, m_params);

        auto begin = endpoints.move(i).second;
        auto end = m_list_size;
        if (i + 1 != size()) {
            end = endpoints.move(i + 1).second;
        }
        auto span = m_source.subspan(m_list_offset + begin, end - begin);

        volatile uint32_t tmp;
        for (auto v: span) {
            tmp = v;
        }
        (void)tmp;
    }

    void swap(block_freq_index& other)
    {
        std::swap(m_params, other.m_params);
        std::swap(m_size, other.m_size);
        m_endpoints.swap(other.m_endpoints);
        std::swap(m_source, other.m_source);
        std::swap(m_list_offset, other.m_list_offset);
        std::swap(m_list_size, other.m_list_size);
    }

    template <typename Visitor>
    void map(Visitor& visit)
    {
        auto lists = m_source.subspan(m_list_offset, m_list_size);
        auto list_span = lists.span();
        visit(m_params, "m_params");
        visit(m_size, "m_size");
        visit(m_num_docs, "m_num_docs");
        visit(m_endpoints, "m_endpoints");
        visit(list_span, "m_lists");
    }

  private:
    global_parameters m_params;
    size_t m_size{0};
    size_t m_num_docs{0};
    bit_vector m_endpoints;
    MemorySource m_source;
    MemorySpan m_endpoint_memory;
    size_t m_list_offset{0};
    size_t m_list_size{0};
};

}  // namespace pisa
