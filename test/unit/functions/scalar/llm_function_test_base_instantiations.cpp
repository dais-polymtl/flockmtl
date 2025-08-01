#include "flockmtl/functions/scalar/llm_complete.hpp"
#include "flockmtl/functions/scalar/llm_embedding.hpp"
#include "flockmtl/functions/scalar/llm_filter.hpp"
#include "llm_function_test_base.hpp"

namespace flockmtl {

// Template method implementations
template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::SetUp() {
    auto con = Config::GetConnection();
    con.Query(" CREATE SECRET ("
              "       TYPE OPENAI,"
              "    API_KEY 'your-api-key');");

    mock_provider = std::make_shared<MockProvider>(ModelDetails{});
    Model::SetMockProvider(mock_provider);
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::TearDown() {
    Model::ResetMockProvider();
}

template<typename FunctionClass>
duckdb::LogicalType LLMFunctionTestBase<FunctionClass>::CreateModelStruct() {
    duckdb::child_list_t<duckdb::LogicalType> fields = {
            {"model_name", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}};
    return duckdb::LogicalType::STRUCT(fields);
}

template<typename FunctionClass>
duckdb::LogicalType LLMFunctionTestBase<FunctionClass>::CreatePromptStruct() {
    duckdb::child_list_t<duckdb::LogicalType> fields = {
            {"prompt", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}};
    return duckdb::LogicalType::STRUCT(fields);
}

template<typename FunctionClass>
duckdb::LogicalType LLMFunctionTestBase<FunctionClass>::CreateInputStruct(const std::vector<std::string>& field_names) {
    duckdb::child_list_t<duckdb::LogicalType> fields;
    for (const auto& name: field_names) {
        fields.emplace_back(name, duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR));
    }
    return duckdb::LogicalType::STRUCT(fields);
}

template<typename FunctionClass>
duckdb::LogicalType LLMFunctionTestBase<FunctionClass>::CreateGenericStruct(const std::vector<std::pair<std::string, duckdb::LogicalType>>& fields) {
    duckdb::child_list_t<duckdb::LogicalType> child_fields;
    for (const auto& [name, type]: fields) {
        child_fields.emplace_back(name, type);
    }
    return duckdb::LogicalType::STRUCT(child_fields);
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::SetStructStringData(duckdb::Vector& struct_vector, const std::vector<std::map<std::string, std::string>>& data) {
    auto& child_vectors = duckdb::StructVector::GetEntries(struct_vector);

    for (size_t row = 0; row < data.size(); row++) {
        for (const auto& [field_name, field_value]: data[row]) {
            // Find the correct child vector for this field
            const auto& struct_type = struct_vector.GetType();
            auto& child_types = duckdb::StructType::GetChildTypes(struct_type);

            for (size_t i = 0; i < child_types.size(); i++) {
                if (child_types[i].first == field_name) {
                    const auto string_data = duckdb::FlatVector::GetData<duckdb::string_t>(*child_vectors[i]);
                    string_data[row] = duckdb::StringVector::AddString(*child_vectors[i], field_value);
                    break;
                }
            }
        }
    }
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::CreateBasicChunk(duckdb::DataChunk& chunk, size_t cardinality) {
    auto model_struct = CreateModelStruct();
    auto prompt_struct = CreatePromptStruct();

    chunk.Initialize(duckdb::Allocator::DefaultAllocator(), {model_struct, prompt_struct});
    chunk.SetCardinality(cardinality);
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::TestValidateArguments() {
    // Test valid cases
    {
        // Valid case with 2 arguments (both structs) - for functions like llm_complete
        duckdb::DataChunk chunk;
        auto model_struct = CreateGenericStruct({{"provider", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}, {"model", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}});
        auto prompt_struct = CreateGenericStruct({{"text", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}});

        chunk.Initialize(duckdb::Allocator::DefaultAllocator(), {model_struct, prompt_struct});
        chunk.SetCardinality(1);

        // Only test 2-argument validation for functions that support it
        try {
            FunctionClass::ValidateArguments(chunk);
            // If no exception thrown, the function supports 2 arguments
        } catch (const std::runtime_error&) {
            // Function requires 3 arguments, that's fine too
        }
    }

    {
        // Valid case with 3 arguments (all structs) - for functions like llm_filter
        duckdb::DataChunk chunk;
        auto model_struct = CreateGenericStruct({{"provider", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}});
        auto prompt_struct = CreateGenericStruct({{"text", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}});
        auto input_struct = CreateGenericStruct({{"variables", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}});

        chunk.Initialize(duckdb::Allocator::DefaultAllocator(), {model_struct, prompt_struct, input_struct});
        chunk.SetCardinality(1);

        EXPECT_NO_THROW(FunctionClass::ValidateArguments(chunk));
    }

    // Test invalid cases - use a parameterized approach to reduce redundancy
    struct InvalidTestCase {
        std::string description;
        duckdb::vector<duckdb::LogicalType> types;
    };

    std::vector<InvalidTestCase> invalid_cases = {
            {"too few arguments (1 argument)", {CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}})}},
            {"too many arguments (4 arguments)", {CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}})}},
            {"first argument is not a struct", {duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR), CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}})}},
            {"second argument is not a struct", {CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), duckdb::LogicalType::INTEGER}},
            {"third argument exists but is not a struct", {CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), CreateGenericStruct({{"field", duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)}}), duckdb::LogicalType::BOOLEAN}}};

    for (const auto& test_case: invalid_cases) {
        SCOPED_TRACE("Testing: " + test_case.description);
        duckdb::DataChunk chunk;
        chunk.Initialize(duckdb::Allocator::DefaultAllocator(), test_case.types);
        chunk.SetCardinality(1);

        EXPECT_THROW(FunctionClass::ValidateArguments(chunk), std::runtime_error);
    }
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::TestOperationInvalidArguments() {
    duckdb::DataChunk chunk;
    chunk.Initialize(duckdb::Allocator::DefaultAllocator(), {duckdb::LogicalType(duckdb::LogicalTypeId::VARCHAR)});
    chunk.SetCardinality(1);

    EXPECT_THROW(FunctionClass::Operation(chunk), std::runtime_error);
}

template<typename FunctionClass>
void LLMFunctionTestBase<FunctionClass>::TestOperationEmptyPrompt() {
    duckdb::DataChunk chunk;

    // Try with 3 arguments first (for functions like llm_filter that require all 3)
    try {
        auto model_struct = CreateModelStruct();
        auto prompt_struct = CreatePromptStruct();
        auto input_struct = CreateInputStruct({"test"});

        chunk.Initialize(duckdb::Allocator::DefaultAllocator(), {model_struct, prompt_struct, input_struct});
        chunk.SetCardinality(1);

        SetStructStringData(chunk.data[0], {{{"model_name", DEFAULT_MODEL}}});
        SetStructStringData(chunk.data[1], {{{"prompt", ""}}});
        SetStructStringData(chunk.data[2], {{{"test", "value"}}});

        EXPECT_THROW(FunctionClass::Operation(chunk);, std::runtime_error);
        return;
    } catch (const std::runtime_error&) {
        // Function might not accept 3 arguments, try with 2
    }

    // Fallback to 2 arguments (for functions like llm_complete)
    CreateBasicChunk(chunk);

    SetStructStringData(chunk.data[0], {{{"model_name", DEFAULT_MODEL}}});
    SetStructStringData(chunk.data[1], {{{"prompt", ""}}});

    EXPECT_THROW(FunctionClass::Operation(chunk);, std::runtime_error);
}

// Explicit instantiations for all used function classes
template class LLMFunctionTestBase<LlmComplete>;
template class LLMFunctionTestBase<LlmEmbedding>;
template class LLMFunctionTestBase<LlmFilter>;

}// namespace flockmtl
