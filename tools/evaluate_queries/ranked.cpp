#include "def.hpp"
#include "query/algorithm/ranked_and_query.hpp"
#include "query/algorithm/ranked_or_query.hpp"

namespace pisa {

#define PISA_RANKED_OR_QUERY_LOOP(SCORER, INDEX, WAND)                                 \
    template <>                                                                        \
    auto query_loop<BOOST_PP_CAT(INDEX, _index),                                       \
                    pisa::ranked_or_query,                                             \
                    wand_data<WAND>,                                                   \
                    SCORER<wand_data<WAND>>>(BOOST_PP_CAT(INDEX, _index) const &index, \
                                             wand_data<WAND> const &,                  \
                                             SCORER<wand_data<WAND>> scorer,           \
                                             std::vector<Query> const &queries,        \
                                             int k)                                    \
        ->std::vector<ResultVector>                                                    \
    {                                                                                  \
        std::vector<ResultVector> results(queries.size());                             \
        auto run = ranked_or_query(k);                                                 \
        for (std::size_t qidx = 0; qidx < queries.size(); ++qidx) {                    \
            auto query = queries[qidx];                                                \
            auto cursors = make_scored_cursors(index, scorer, query);                  \
            run(gsl::make_span(cursors), index.num_docs());                            \
            results[qidx] = run.topk();                                                \
        }                                                                              \
        return results;                                                                \
    }

#define LOOP_BODY(R, DATA, T)                                \
    PISA_RANKED_OR_QUERY_LOOP(bm25, T, wand_data_raw)        \
    PISA_RANKED_OR_QUERY_LOOP(dph, T, wand_data_raw)         \
    PISA_RANKED_OR_QUERY_LOOP(pl2, T, wand_data_raw)         \
    PISA_RANKED_OR_QUERY_LOOP(qld, T, wand_data_raw)         \
    PISA_RANKED_OR_QUERY_LOOP(bm25, T, wand_data_compressed) \
    PISA_RANKED_OR_QUERY_LOOP(dph, T, wand_data_compressed)  \
    PISA_RANKED_OR_QUERY_LOOP(pl2, T, wand_data_compressed)  \
    PISA_RANKED_OR_QUERY_LOOP(qld, T, wand_data_compressed)  \
/**/
BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, PISA_INDEX_TYPES);
#undef LOOP_BODY
#undef PISA_RANKED_OR_QUERY_LOOP

#define PISA_RANKED_AND_QUERY_LOOP(SCORER, INDEX, WAND)                                \
    template <>                                                                        \
    auto query_loop<BOOST_PP_CAT(INDEX, _index),                                       \
                    pisa::ranked_and_query,                                            \
                    wand_data<WAND>,                                                   \
                    SCORER<wand_data<WAND>>>(BOOST_PP_CAT(INDEX, _index) const &index, \
                                             wand_data<WAND> const &,                  \
                                             SCORER<wand_data<WAND>> scorer,           \
                                             std::vector<Query> const &queries,        \
                                             int k)                                    \
        ->std::vector<ResultVector>                                                    \
    {                                                                                  \
        std::vector<ResultVector> results(queries.size());                             \
        auto run = ranked_and_query(k);                                                \
        for (std::size_t qidx = 0; qidx < queries.size(); ++qidx) {                    \
            auto query = queries[qidx];                                                \
            auto cursors = make_scored_cursors(index, scorer, query);                  \
            run(gsl::make_span(cursors), index.num_docs());                            \
            results[qidx] = run.topk();                                                \
        }                                                                              \
        return results;                                                                \
    }

#define LOOP_BODY(R, DATA, T)                                \
    PISA_RANKED_AND_QUERY_LOOP(bm25, T, wand_data_raw)        \
    PISA_RANKED_AND_QUERY_LOOP(dph, T, wand_data_raw)         \
    PISA_RANKED_AND_QUERY_LOOP(pl2, T, wand_data_raw)         \
    PISA_RANKED_AND_QUERY_LOOP(qld, T, wand_data_raw)         \
    PISA_RANKED_AND_QUERY_LOOP(bm25, T, wand_data_compressed) \
    PISA_RANKED_AND_QUERY_LOOP(dph, T, wand_data_compressed)  \
    PISA_RANKED_AND_QUERY_LOOP(pl2, T, wand_data_compressed)  \
    PISA_RANKED_AND_QUERY_LOOP(qld, T, wand_data_compressed)  \
/**/
BOOST_PP_SEQ_FOR_EACH(LOOP_BODY, _, PISA_INDEX_TYPES);
#undef LOOP_BODY
#undef PISA_RANKED_AND_QUERY_LOOP

} // namespace pisa