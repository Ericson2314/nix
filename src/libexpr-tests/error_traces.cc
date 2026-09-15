#include <gmock/gmock.h>
#include <gtest/gtest.h>

#include "nix/expr/tests/libexpr.hh"
#include "nix/util/finally.hh"

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

TEST_F(StructuredErrorTest, assertFailed)
{
    EXPECT_EQ(
        failureJSON("assert false; true"), (nlohmann::json{{"type", "assertion-failed"}, {"expression", "false"}}));
}

TEST_F(StructuredErrorTest, infiniteRecursion)
{
    EXPECT_EQ(failureJSON("let x = x; in x"), (nlohmann::json{{"type", "infinite-recursion"}}));
}

TEST_F(StructuredErrorTest, unstructuredHasNone)
{
    EXPECT_EQ(failureJSON("builtins.baseNameOf 1 2"), std::nullopt);
}

class StructuredTraceTest : public LibExprTest
{
protected:
    /**
     * Evaluate `input`, which must fail, and return the structured data
     * of its trace frames, innermost first, with `null` for frames that
     * have none.
     */
    nlohmann::json failureFrames(std::string input)
    {
        bool oldShowTrace = loggerSettings.showTrace.get();
        loggerSettings.showTrace.assign(true);
        Finally restoreShowTrace([oldShowTrace] { loggerSettings.showTrace.assign(oldShowTrace); });
        try {
            eval(input);
        } catch (Error & e) {
            auto frames = nlohmann::json::array();
            for (auto & trace : e.info().traces)
                frames.push_back(trace.data ? *trace.data : nlohmann::json());
            return frames;
        }
        ADD_FAILURE() << "expected '" << input << "' to fail";
        return nullptr;
    }
};

TEST_F(StructuredTraceTest, callingBuiltin)
{
    auto frames = failureFrames("builtins.length (builtins.attrNames 1)");
    EXPECT_THAT(frames, testing::Contains(nlohmann::json{{"type", "calling-builtin"}, {"name", "attrNames"}}));
    EXPECT_THAT(frames, testing::Contains(nlohmann::json{{"type", "calling-builtin"}, {"name", "length"}}));
}

TEST_F(StructuredTraceTest, builtinArgument)
{
    auto frames = failureFrames("builtins.length 1");
    EXPECT_THAT(
        frames,
        testing::Contains(
            nlohmann::json{{"type", "evaluating-builtin-argument"}, {"builtin", "length"}, {"argument", 1}}));
}

TEST_F(StructuredTraceTest, callingFunctionAndAttribute)
{
    auto frames = failureFrames("let f = x: x.a; in { a = f 1; }.a");
    EXPECT_THAT(frames, testing::Contains(nlohmann::json{{"type", "calling-function"}, {"name", "f"}}));
    EXPECT_THAT(frames, testing::Contains(nlohmann::json{{"type", "call-site"}}));
    EXPECT_THAT(frames, testing::Contains(nlohmann::json{{"type", "evaluating-attribute"}, {"attribute", "a"}}));
}

} /* namespace nix */
