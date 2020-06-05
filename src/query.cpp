#include "query.hpp"

#include <algorithm>
#include <bitset>
#include <fstream>
#include <iostream>

#include <boost/filesystem.hpp>
#include <fmt/format.h>
#include <nlohmann/json.hpp>
#include <range/v3/algorithm/sort.hpp>
#include <range/v3/algorithm/unique.hpp>

namespace pisa {

[[nodiscard]] auto first_equal_to(std::size_t k)
{
    return [k](auto&& pair) { return pair.first == k; };
}

void RequestFlagSet::remove(RequestFlag flag)
{
    flags ^= static_cast<std::size_t>(flag);
}

auto RequestFlagSet::operator^(RequestFlag flag) -> RequestFlagSet
{
    auto flags_copy = flags;
    flags_copy ^= static_cast<std::size_t>(flag);
    return RequestFlagSet{flags_copy};
}

auto RequestFlagSet::contains(RequestFlag flag) -> bool
{
    return (flags & static_cast<std::uint32_t>(flag)) == static_cast<std::uint32_t>(flag);
}

auto operator|(RequestFlag lhs, RequestFlag rhs) -> RequestFlagSet
{
    return RequestFlagSet{static_cast<std::uint32_t>(lhs) | static_cast<std::uint32_t>(lhs)};
}

auto operator&(RequestFlag lhs, RequestFlag rhs) -> RequestFlagSet
{
    return RequestFlagSet{static_cast<std::uint32_t>(lhs) & static_cast<std::uint32_t>(lhs)};
}

auto operator|(RequestFlagSet lhs, RequestFlag rhs) -> RequestFlagSet
{
    return RequestFlagSet{lhs.flags | static_cast<std::uint32_t>(rhs)};
}

auto operator&(RequestFlagSet lhs, RequestFlag rhs) -> RequestFlagSet
{
    return RequestFlagSet{lhs.flags & static_cast<std::uint32_t>(rhs)};
}

auto operator|=(RequestFlagSet& lhs, RequestFlag rhs) -> RequestFlagSet&
{
    lhs = lhs | rhs;
    return lhs;
}

auto operator&=(RequestFlagSet& lhs, RequestFlag rhs) -> RequestFlagSet&
{
    lhs = lhs & rhs;
    return lhs;
}

QueryRequest::QueryRequest(QueryContainer const& data, std::size_t k, RequestFlagSet flags)
    : m_k(k), m_threshold(data.threshold(k))
{
    if (auto term_ids = data.term_ids(); term_ids) {
        std::map<TermId, std::size_t> counts;
        for (auto term_id: *term_ids) {
            counts[term_id] += 1;
        }
        for (auto [term_id, count]: counts) {
            m_term_ids.push_back(term_id);
            m_term_weights.push_back(static_cast<float>(count));
        }
        if (auto selection = data.selection(k); selection && flags.contains(RequestFlag::Selection)) {
            std::vector<TermId> selected_terms;
            std::transform(
                selection->selected_terms.begin(),
                selection->selected_terms.end(),
                std::back_inserter(selected_terms),
                [&](auto term_position) { return (*term_ids).at(term_position); });
            ranges::sort(selected_terms);
            selected_terms.erase(ranges::unique(selected_terms), selected_terms.end());
            std::vector<TermPair> selected_pairs;
            std::transform(
                selection->selected_pairs.begin(),
                selection->selected_pairs.end(),
                std::back_inserter(selected_pairs),
                [&](auto term_positions) {
                    return TermPair(
                        (*term_ids).at(std::get<0>(term_positions)),
                        (*term_ids).at(std::get<1>(term_positions)));
                });
            ranges::sort(selected_pairs);
            selected_pairs.erase(ranges::unique(selected_pairs), selected_pairs.end());
            m_selection = Selection<TermId>{
                .selected_terms = std::move(selected_terms),
                .selected_pairs = std::move(selected_pairs),
            };
        }
        if (not flags.contains(RequestFlag::Threshold)) {
            m_threshold.reset();
        }
        if (not flags.contains(RequestFlag::Weights)) {
            std::fill(m_term_weights.begin(), m_term_weights.end(), 1.0);
        }
    } else {
        throw std::domain_error("Query not parsed.");
    }
}

auto QueryRequest::term_ids() const -> gsl::span<std::uint32_t const>
{
    return gsl::span<std::uint32_t const>(m_term_ids);
}

auto QueryRequest::term_weights() const -> gsl::span<float const>
{
    return gsl::span<float const>(m_term_weights);
}

auto QueryRequest::threshold() const -> std::optional<float>
{
    return m_threshold;
}

auto QueryRequest::selection() const -> std::optional<Selection<TermId>>
{
    return m_selection;
}

auto QueryRequest::k() const -> std::size_t
{
    return m_k;
}

struct QueryContainerInner {
    std::optional<std::string> id;
    std::optional<std::string> query_string;
    std::optional<std::vector<std::string>> processed_terms;
    std::optional<std::vector<std::uint32_t>> term_ids;
    std::vector<std::pair<std::size_t, float>> thresholds;
    std::vector<std::pair<std::size_t, Selection<std::size_t>>> selections;

    [[nodiscard]] auto operator==(QueryContainerInner const& other) const noexcept -> bool
    {
        return id == other.id && query_string == other.query_string
            && processed_terms == other.processed_terms && term_ids == other.term_ids
            && thresholds == other.thresholds && selections == other.selections;
    }
};

QueryContainer::QueryContainer() : m_data(std::make_unique<QueryContainerInner>()) {}

QueryContainer::QueryContainer(QueryContainer const& other)
    : m_data(std::make_unique<QueryContainerInner>(*other.m_data))
{}
QueryContainer::QueryContainer(QueryContainer&&) noexcept = default;
QueryContainer& QueryContainer::operator=(QueryContainer const& other)
{
    this->m_data = std::make_unique<QueryContainerInner>(*other.m_data);
    return *this;
}
QueryContainer& QueryContainer::operator=(QueryContainer&&) noexcept = default;
QueryContainer::~QueryContainer() = default;

auto QueryContainer::operator==(QueryContainer const& other) const noexcept -> bool
{
    return *m_data == *other.m_data;
}

auto QueryContainer::raw(std::string query_string) -> QueryContainer
{
    QueryContainer query;
    query.m_data->query_string = std::move(query_string);
    return query;
}

auto QueryContainer::from_terms(
    std::vector<std::string> terms, std::optional<TermProcessorFn> term_processor) -> QueryContainer
{
    QueryContainer query;
    query.m_data->processed_terms = std::vector<std::string>{};
    auto& processed_terms = *query.m_data->processed_terms;
    for (auto&& term: terms) {
        if (term_processor) {
            auto fn = *term_processor;
            if (auto processed = fn(std::move(term)); processed) {
                processed_terms.push_back(std::move(*processed));
            }
        } else {
            processed_terms.push_back(std::move(term));
        }
    }
    return query;
}

auto QueryContainer::from_term_ids(std::vector<std::uint32_t> term_ids) -> QueryContainer
{
    QueryContainer query;
    query.m_data->term_ids = std::move(term_ids);
    return query;
}

auto QueryContainer::id() const noexcept -> std::optional<std::string> const&
{
    return m_data->id;
}
auto QueryContainer::string() const noexcept -> std::optional<std::string> const&
{
    return m_data->query_string;
}
auto QueryContainer::terms() const noexcept -> std::optional<std::vector<std::string>> const&
{
    return m_data->processed_terms;
}

auto QueryContainer::term_ids() const noexcept -> std::optional<std::vector<std::uint32_t>> const&
{
    return m_data->term_ids;
}

auto QueryContainer::threshold(std::size_t k) const noexcept -> std::optional<float>
{
    auto pos = std::find_if(m_data->thresholds.begin(), m_data->thresholds.end(), first_equal_to(k));
    if (pos == m_data->thresholds.end()) {
        return std::nullopt;
    }
    return std::make_optional(pos->second);
}

auto QueryContainer::thresholds() const noexcept -> std::vector<std::pair<std::size_t, float>> const&
{
    return m_data->thresholds;
}

auto QueryContainer::selection(std::size_t k) const noexcept -> std::optional<Selection<std::size_t>>
{
    auto pos = std::find_if(m_data->selections.begin(), m_data->selections.end(), first_equal_to(k));
    if (pos == m_data->selections.end()) {
        return std::nullopt;
    }
    return std::make_optional(pos->second);
}

auto QueryContainer::selections() const noexcept
    -> std::vector<std::pair<std::size_t, Selection<std::size_t>>> const&
{
    return m_data->selections;
}

auto QueryContainer::string(std::string raw_query) -> QueryContainer&
{
    m_data->query_string = std::move(raw_query);
    return *this;
}

auto QueryContainer::parse(ParseFn parse_fn) -> QueryContainer&
{
    if (not m_data->query_string) {
        throw std::domain_error("Cannot parse, query string not set");
    }
    auto parsed_terms = parse_fn(*m_data->query_string);
    std::vector<std::string> processed_terms;
    std::vector<std::uint32_t> term_ids;
    for (auto&& term: parsed_terms) {
        processed_terms.push_back(std::move(term.term));
        term_ids.push_back(term.id);
    }
    m_data->term_ids = std::move(term_ids);
    m_data->processed_terms = std::move(processed_terms);
    return *this;
}

auto QueryContainer::add_threshold(std::size_t k, float score) -> bool
{
    if (auto pos =
            std::find_if(m_data->thresholds.begin(), m_data->thresholds.end(), first_equal_to(k));
        pos != m_data->thresholds.end()) {
        pos->second = score;
        return true;
    }
    m_data->thresholds.emplace_back(k, score);
    return false;
}

auto QueryContainer::add_selection(std::size_t k, Selection<std::size_t> selection) -> bool
{
    if (auto pos =
            std::find_if(m_data->selections.begin(), m_data->selections.end(), first_equal_to(k));
        pos != m_data->selections.end()) {
        pos->second = std::move(selection);
        return true;
    }
    m_data->selections.emplace_back(k, std::move(selection));
    return false;
}

auto QueryContainer::query(std::size_t k, RequestFlagSet flags) const -> QueryRequest
{
    return QueryRequest(*this, k, flags);
}

template <typename T>
[[nodiscard]] auto get(nlohmann::json const& node, std::string_view field) -> std::optional<T>
{
    if (auto pos = node.find(field); pos != node.end()) {
        try {
            return std::make_optional(pos->get<T>());
        } catch (nlohmann::detail::exception const& err) {
            throw std::runtime_error(fmt::format("Requested field {} is of wrong type", field));
        }
    }
    return std::optional<T>{};
}

auto QueryContainer::from_json(std::string_view json_string) -> QueryContainer
{
    try {
        auto json = nlohmann::json::parse(json_string);
        QueryContainer query;
        QueryContainerInner& data = *query.m_data;
        bool at_least_one_required = false;
        if (auto id = get<std::string>(json, "id"); id) {
            data.id = std::move(id);
        }
        if (auto raw = get<std::string>(json, "query"); raw) {
            data.query_string = std::move(raw);
            at_least_one_required = true;
        }
        if (auto terms = get<std::vector<std::string>>(json, "terms"); terms) {
            data.processed_terms = std::move(terms);
            at_least_one_required = true;
        }
        if (auto term_ids = get<std::vector<std::uint32_t>>(json, "term_ids"); term_ids) {
            data.term_ids = std::move(term_ids);
            at_least_one_required = true;
        }
        if (auto thresholds = json.find("thresholds"); thresholds != json.end()) {
            auto raise_error = [&]() {
                throw std::runtime_error(
                    fmt::format("Field \"thresholds\" is invalid: {}", thresholds->dump()));
            };
            if (not thresholds->is_array()) {
                raise_error();
            }
            for (auto&& threshold_entry: *thresholds) {
                if (not threshold_entry.is_object()) {
                    raise_error();
                }
                auto k = get<std::size_t>(threshold_entry, "k");
                auto score = get<float>(threshold_entry, "score");
                if (not k or not score) {
                    raise_error();
                }
                data.thresholds.emplace_back(*k, *score);
            }
        }
        if (auto selections = json.find("selections"); selections != json.end()) {
            auto raise_error = [&]() {
                throw std::runtime_error(
                    fmt::format("Field \"selections\" is invalid: {}", selections->dump()));
            };
            if (not selections->is_array()) {
                raise_error();
            }
            for (auto&& selections_entry: *selections) {
                if (not selections_entry.is_object()) {
                    raise_error();
                }
                auto k = get<std::size_t>(selections_entry, "k");
                auto masks = get<std::vector<std::size_t>>(selections_entry, "intersections");
                if (not k or not masks) {
                    raise_error();
                }
                std::vector<std::size_t> selected_terms;
                std::vector<std::array<std::size_t, 2>> selected_pairs;
                for (auto mask: *masks) {
                    std::bitset<64> m(mask);
                    if (m.count() > 2) {
                        std::runtime_error("Only single term and pair selections are supported");
                    }
                    std::array<std::size_t, 2> term_positions{};
                    std::size_t pos = 0;
                    auto out = term_positions.begin();
                    while (m.count() > 0) {
                        if (m.test(pos)) {
                            m.reset(pos);
                            *out++ = pos;
                        }
                        pos += 1;
                    }
                    if (out == term_positions.end()) {
                        selected_pairs.push_back(term_positions);
                    } else {
                        selected_terms.push_back(std::get<0>(term_positions));
                    }
                }
                data.selections.emplace_back(
                    *k,
                    Selection<std::size_t>{
                        .selected_terms = selected_terms, .selected_pairs = selected_pairs});
            }
        }
        if (not at_least_one_required) {
            throw std::invalid_argument(fmt::format(
                "JSON must have either raw query, terms, or term IDs: {}", json_string));
        }
        return query;
    } catch (nlohmann::detail::exception const& err) {
        throw std::runtime_error(
            fmt::format("Failed to parse JSON: `{}`: {}", json_string, err.what()));
    }
}
auto QueryContainer::to_json_string(int indent) const -> std::string
{
    return to_json().dump(indent);
}

auto QueryContainer::to_json() const -> nlohmann::json
{
    nlohmann::json json;
    if (auto id = m_data->id; id) {
        json["id"] = *id;
    }
    if (auto raw = m_data->query_string; raw) {
        json["query"] = *raw;
    }
    if (auto terms = m_data->processed_terms; terms) {
        json["terms"] = *terms;
    }
    if (auto term_ids = m_data->term_ids; term_ids) {
        json["term_ids"] = *term_ids;
    }
    if (not m_data->thresholds.empty()) {
        auto thresholds = nlohmann::json::array();
        for (auto&& [k, score]: m_data->thresholds) {
            auto entry = nlohmann::json::object();
            entry["k"] = k;
            entry["score"] = score;
            thresholds.push_back(std::move(entry));
        }
        json["thresholds"] = thresholds;
    }
    if (not m_data->selections.empty()) {
        auto selections = nlohmann::json::array();
        for (auto&& [k, intersections]: m_data->selections) {
            auto entry = nlohmann::json::object();
            entry["k"] = k;
            std::vector<std::size_t> intersections_vec;
            for (auto term: intersections.selected_terms) {
                intersections_vec.push_back(1U << term);
            }
            for (auto [left, right]: intersections.selected_pairs) {
                intersections_vec.push_back((1U << left) | (1U << right));
            }
            std::sort(intersections_vec.begin(), intersections_vec.end());
            entry["intersections"] = intersections_vec;
            selections.push_back(std::move(entry));
        }
        json["selections"] = selections;
    }
    return json;
}

auto QueryContainer::from_colon_format(std::string_view line) -> QueryContainer
{
    auto pos = std::find(line.begin(), line.end(), ':');
    QueryContainer query;
    QueryContainerInner& data = *query.m_data;
    if (pos == line.end()) {
        data.query_string = std::string(line);
    } else {
        data.id = std::string(line.begin(), pos);
        data.query_string = std::string(std::next(pos), line.end());
    }
    return query;
}

void QueryContainer::filter_terms(gsl::span<std::size_t const> term_positions)
{
    auto const& processed_terms = m_data->processed_terms;
    auto const& term_ids = m_data->term_ids;
    if (not processed_terms && not term_ids) {
        return;
    }
    auto query_length = 0;
    if (processed_terms) {
        query_length = processed_terms->size();
    } else if (term_ids) {
        query_length = term_ids->size();
    }
    std::vector<std::string> filtered_terms;
    std::vector<TermId> filtered_ids;
    for (auto position: term_positions) {
        if (position >= query_length) {
            throw std::out_of_range("Passed term position out of range");
        }
        if (processed_terms) {
            filtered_terms.push_back(std::move((*m_data->processed_terms)[position]));
        }
        if (term_ids) {
            filtered_ids.push_back((*m_data->term_ids)[position]);
        }
    }
    if (processed_terms) {
        m_data->processed_terms = filtered_terms;
    }
    if (term_ids) {
        m_data->term_ids = filtered_ids;
    }
}

auto QueryReader::from_file(std::string const& file) -> QueryReader
{
    if (not boost::filesystem::exists(file)) {
        throw std::runtime_error(fmt::format("File not found: {}", file));
    }
    auto input = std::make_unique<std::ifstream>(file);
    if (input->good()) {
        auto& ref = *input;
        return QueryReader(std::move(input), ref);
    }
    throw std::runtime_error(fmt::format("Unable to read from file: {}", file));
}

auto QueryReader::from_stdin() -> QueryReader
{
    return QueryReader(nullptr, std::cin);
}

QueryReader::QueryReader(std::unique_ptr<std::istream> input, std::istream& stream_ref)
    : m_stream(std::move(input)), m_stream_ref(stream_ref)
{}

auto QueryReader::next_query(QueryReader& reader) -> std::optional<QueryContainer>
{
    if (std::getline(reader.m_stream_ref, reader.m_line_buf)) {
        if (reader.m_format) {
            if (*reader.m_format == Format::Json) {
                return QueryContainer::from_json(reader.m_line_buf);
            }
            return QueryContainer::from_colon_format(reader.m_line_buf);
        }
        try {
            auto query = QueryContainer::from_json(reader.m_line_buf);
            reader.m_format = Format::Json;
            return query;
        } catch (std::exception const& err) {
            reader.m_format = Format::Colon;
            return QueryContainer::from_colon_format(reader.m_line_buf);
        }
    }
    return std::nullopt;
}
auto QueryReader::next() -> std::optional<QueryContainer>
{
    while (true) {
        auto query = QueryReader::next_query(*this);
        if (not query) {
            return std::nullopt;
        }
        auto container = *query;
        for (auto&& fn: m_filter_functions) {
            if (not fn(container)) {
                continue;
            }
        }
        for (auto&& fn: m_map_functions) {
            container = fn(std::move(container));
        }
        return container;
    }
}

auto QueryReader::map(typename QueryReader::map_function_type fn) && -> QueryReader
{
    m_map_functions.push_back(std::move(fn));
    return std::move(*this);
}
auto QueryReader::filter(typename QueryReader::filter_function_type fn) && -> QueryReader
{
    m_filter_functions.push_back(std::move(fn));
    return std::move(*this);
}

}  // namespace pisa