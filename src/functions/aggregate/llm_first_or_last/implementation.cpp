#include "flockmtl/functions/aggregate/llm_first_or_last.hpp"

namespace flockmtl {

int LlmFirstOrLast::GetFirstOrLastTupleId(const nlohmann::json& tuples) {
    nlohmann::json data;
    const auto prompt = PromptManager::Render(user_query, tuples, function_type, model.GetModelDetails().tuple_format);
    model.AddCompletionRequest(prompt, 1, OutputType::INTEGER);
    auto response = model.CollectCompletions()[0];
    return response["items"][0];
}

nlohmann::json LlmFirstOrLast::Evaluate(nlohmann::json& tuples) {
    auto batch_tuples = nlohmann::json::array();
    int start_index = 0;
    model = Model(model_details);
    auto batch_size = std::min<int>(model.GetModelDetails().batch_size, static_cast<int>(tuples.size()));

    if (batch_size <= 0) {
        throw std::runtime_error("Batch size must be greater than zero");
    }

    do {

        for (auto i = 0; i < batch_size && start_index < static_cast<int>(tuples.size()); i++) {
            batch_tuples.push_back(tuples[start_index]);
        }

        start_index += batch_size;

        try {
            auto result_idx = GetFirstOrLastTupleId(batch_tuples);
            batch_tuples.clear();
            batch_tuples.push_back(tuples[result_idx]);
        } catch (const ExceededMaxOutputTokensError&) {
            start_index -= batch_size;// Retry the current batch with reduced size
            batch_size = static_cast<int>(batch_size * 0.9);
            if (batch_size <= 0) {
                throw std::runtime_error("Batch size reduced to zero, unable to process tuples");
            }
        }

    } while (start_index < static_cast<int>(tuples.size()));

    batch_tuples[0].erase("flockmtl_tuple_id");

    return batch_tuples[0];
}

void LlmFirstOrLast::FinalizeResults(duckdb::Vector& states, duckdb::AggregateInputData& aggr_input_data,
                                     duckdb::Vector& result, idx_t count, idx_t offset,
                                     AggregateFunctionType function_type) {
    const auto states_vector = reinterpret_cast<AggregateFunctionState**>(duckdb::FlatVector::GetData<duckdb::data_ptr_t>(states));

    for (idx_t i = 0; i < count; i++) {
        auto idx = i + offset;
        auto* state = states_vector[idx];

        if (state && !state->value->empty()) {
            auto tuples_with_ids = nlohmann::json::array();
            for (auto j = 0; j < static_cast<int>(state->value->size()); j++) {
                auto tuple_with_id = (*state->value)[j];
                tuple_with_id["flockmtl_tuple_id"] = j;
                tuples_with_ids.push_back(tuple_with_id);
            }
            LlmFirstOrLast function_instance;
            function_instance.function_type = function_type;
            auto response = function_instance.Evaluate(tuples_with_ids);
            result.SetValue(idx, response.dump());
        } else {
            result.SetValue(idx, nullptr);// Empty JSON object for null/empty states
        }
    }
}

}// namespace flockmtl
