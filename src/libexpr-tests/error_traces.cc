#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"

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

class StructuredErrorTest : public LibExprTest
{
protected:
    /**
     * Evaluate `input`, which must fail, and return the failure's JSON.
     */
    std::optional<nlohmann::json> failureJSON(std::string input)
    {
        try {
            eval(input);
        } catch (Error & e) {
            return e.toJSON();
        }
        ADD_FAILURE() << "expected '" << input << "' to fail";
        return std::nullopt;
    }
};

TEST_F(StructuredErrorTest, undefinedVariable)
{
    EXPECT_EQ(failureJSON("puppy"), (nlohmann::json{{"type", "undefined-variable"}, {"name", "puppy"}}));
}

TEST_F(StructuredErrorTest, thrown)
{
    EXPECT_EQ(failureJSON("builtins.throw \"boom\""), (nlohmann::json{{"type", "throw"}, {"message", "boom"}}));
}

TEST_F(StructuredErrorTest, abort)
{
    EXPECT_EQ(failureJSON("builtins.abort \"boom\""), (nlohmann::json{{"type", "abort"}, {"message", "boom"}}));
}

TEST_F(StructuredErrorTest, unexpectedType)
{
    EXPECT_EQ(
        failureJSON("builtins.attrNames 1"),
        (nlohmann::json{{"type", "unexpected-type"}, {"expected", "set"}, {"found", "integer"}, {"value", "1"}}));
}

TEST_F(StructuredErrorTest, infiniteRecursion)
{
    EXPECT_EQ(failureJSON("let x = x; in x"), (nlohmann::json{{"type", "infinite-recursion"}}));
}

TEST_F(StructuredErrorTest, unstructuredHasNone)
{
    EXPECT_EQ(failureJSON("builtins.baseNameOf 1 2"), std::nullopt);
}

} /* namespace nix */
