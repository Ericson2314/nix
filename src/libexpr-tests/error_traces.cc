#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/util/finally.hh"
#include "nix/util/tests/json-characterization.hh"

#include <nlohmann/json.hpp>

namespace nix {

// Testing eval of PrimOp's
class ErrorTraceTest : public LibExprTest
{};

TEST_F(ErrorTraceTest, TraceBuilder)
{
    using namespace testing;

    ASSERT_THROW(state.error<EvalError>("puppy").debugThrow(), EvalError);

    ASSERT_THROW(state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow(), EvalError);

    ASSERT_THROW(
        try {
            try {
                state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow();
            } catch (Error & e) {
                e.addTrace(state.positions[noPos], "beans");
                throw;
            }
        } catch (BaseError & e) {
            ASSERT_EQ(PrintToString(e.renderMessage()), PrintToString(HintFmt("puppy")));
            auto trace = e.info().traces.rbegin();
            ASSERT_EQ(e.info().traces.size(), 2u);
            ASSERT_EQ(PrintToString(trace->hint), PrintToString(HintFmt("doggy")));
            trace++;
            ASSERT_EQ(PrintToString(trace->hint), PrintToString(HintFmt("beans")));
            throw;
        },
        EvalError);
}

TEST_F(ErrorTraceTest, NestedThrows)
{
    try {
        state.error<EvalError>("puppy").withTrace(noPos, "doggy").debugThrow();
    } catch (BaseError & e) {
        try {
            state.error<EvalError>("beans").debugThrow();
        } catch (Error & e2) {
            e.addTrace(state.positions[noPos], "beans2");
            // e2.addTrace(state.positions[noPos], "Something", "");
            ASSERT_TRUE(e.info().traces.size() == 2u);
            ASSERT_TRUE(e2.info().traces.size() == 0u);
            ASSERT_FALSE(&e.info() == &e2.info());
        }
    }
}

/**
 * The JSON of structured errors and trace frames, as golden files that
 * double as the examples in the manual and are checked against the JSON
 * schemas there.
 */
class StructuredErrorTest : public LibExprTest, public virtual CharacterizationTest
{
protected:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return getUnitTestData() / "structured-error" / testStem;
    }

    /**
     * Evaluate `input`, which must fail, and return the failure's JSON.
     */
    nlohmann::json failureJSON(std::string input)
    {
        try {
            eval(input);
        } catch (Error & e) {
            auto j = e.toJSON();
            if (!j)
                ADD_FAILURE() << "expected '" << input << "' to fail with a structured error";
            return j.value_or(nullptr);
        }
        ADD_FAILURE() << "expected '" << input << "' to fail";
        return nullptr;
    }
};

#define STRUCTURED_ERROR_TEST(name, input)               \
    TEST_F(StructuredErrorTest, name)                    \
    {                                                    \
        writeJsonTest(*this, #name, failureJSON(input)); \
    }

STRUCTURED_ERROR_TEST(throw, "builtins.throw \"boom\"")
STRUCTURED_ERROR_TEST(abort, "builtins.abort \"boom\"")
STRUCTURED_ERROR_TEST(undefined_variable, "puppy")
STRUCTURED_ERROR_TEST(unexpected_type, "builtins.attrNames 1")
STRUCTURED_ERROR_TEST(assertion_failed, "assert false; true")
STRUCTURED_ERROR_TEST(infinite_recursion, "let x = x; in x")

TEST_F(StructuredErrorTest, unstructuredHasNone)
{
    try {
        eval("builtins.baseNameOf 1 2");
    } catch (Error & e) {
        EXPECT_EQ(e.toJSON(), std::nullopt);
        return;
    }
    ADD_FAILURE() << "expected a failure";
}

class StructuredTraceTest : public LibExprTest, public virtual CharacterizationTest
{
protected:
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return getUnitTestData() / "trace-frame" / testStem;
    }

    /**
     * Evaluate `input`, which must fail, and return the structured data
     * of the first trace frame of the given `type`.
     */
    nlohmann::json frameOfType(std::string input, std::string_view type)
    {
        bool oldShowTrace = loggerSettings.showTrace.get();
        loggerSettings.showTrace.assign(true);
        Finally restoreShowTrace([oldShowTrace] { loggerSettings.showTrace.assign(oldShowTrace); });
        try {
            eval(input);
        } catch (Error & e) {
            for (auto & trace : e.info().traces)
                if (trace.data && (*trace.data)["type"] == type)
                    return *trace.data;
            ADD_FAILURE() << "no frame of type '" << type << "' in the failure of '" << input << "'";
            return nullptr;
        }
        ADD_FAILURE() << "expected '" << input << "' to fail";
        return nullptr;
    }
};

#define STRUCTURED_TRACE_TEST(name, input, type)               \
    TEST_F(StructuredTraceTest, name)                          \
    {                                                          \
        writeJsonTest(*this, #name, frameOfType(input, type)); \
    }

STRUCTURED_TRACE_TEST(calling_builtin, "builtins.length (builtins.attrNames 1)", "calling-builtin")
STRUCTURED_TRACE_TEST(calling_function, "let f = x: x.a; in f 1", "calling-function")
STRUCTURED_TRACE_TEST(calling_anonymous_function, "(x: x.a) 1", "calling-function")
STRUCTURED_TRACE_TEST(call_site, "let f = x: x.a; in f 1", "call-site")
STRUCTURED_TRACE_TEST(evaluating_attribute, "{ a = { b = builtins.throw \"x\"; }; }.a.b", "evaluating-attribute")
STRUCTURED_TRACE_TEST(evaluating_builtin_argument, "builtins.length 1", "evaluating-builtin-argument")
STRUCTURED_TRACE_TEST(evaluating_operand, "true && 1", "evaluating-operand")
STRUCTURED_TRACE_TEST(evaluating_assertion_condition, "assert 1 == 2; true", "evaluating-assertion-condition")

} // namespace nix
