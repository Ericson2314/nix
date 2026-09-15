#pragma once

#include "nix/util/error.hh"
#include "nix/util/pos-idx.hh"
#include "nix/store/path.hh"
#include "nix/expr/value.hh"

namespace nix {

struct Env;
struct Expr;
struct Value;

class EvalState;
template<class T>
class EvalErrorBuilder;

/**
 * Base class for all errors that occur during evaluation.
 *
 * Most subclasses should inherit from `EvalError` instead of this class.
 */
class EvalBaseError : public CloneableError<EvalBaseError, UnstructuredError>
{
    template<class T>
    friend class EvalErrorBuilder;

    void anchor() override;

public:
    EvalState & state;

    EvalBaseError(EvalState & state, ErrorInfo && errorInfo, HintFmt hint)
        : CloneableError(std::move(errorInfo), std::move(hint))
        , state(state)
    {
    }

    /**
     * For structured subclasses, which render their own message.
     */
    EvalBaseError(EvalState & state, ErrorInfo && errorInfo)
        : CloneableError(NoHint{}, std::move(errorInfo))
        , state(state)
    {
    }

    template<typename... Args>
    explicit EvalBaseError(EvalState & state, const std::string & formatString, const Args &... formatArgs)
        : CloneableError(formatString, formatArgs...)
        , state(state)
    {
    }
};

/**
 * `EvalError` is the base class for almost all errors that occur during evaluation.
 *
 * All instances of `EvalError` should show a degree of purity that allows them to be
 * cached in pure mode. This means that they should not depend on the configuration or the overall environment.
 */
MakeError(EvalError, EvalBaseError);
MakeError(ParseError, UnstructuredError);
MakeError(AssertionError, EvalError);
MakeError(TypeError, EvalError);

/* The structured errors below each carry the facts of the matter as
   fields, render their message from them, and expose them as JSON with a
   `type` discriminator naming the kind of error. */

/**
 * `builtins.throw`.
 */
class ThrownError : public CloneableError<ThrownError, AssertionError>
{
    void anchor() override;

public:
    /**
     * The message the expression threw.
     */
    std::string text;

    ThrownError(EvalState & state, std::string text);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * `builtins.abort`.
 */
class Abort : public CloneableError<Abort, EvalError>
{
    void anchor() override;

public:
    /**
     * The message the expression aborted with.
     */
    std::string text;

    Abort(EvalState & state, std::string text);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

class UndefinedVarError : public CloneableError<UndefinedVarError, EvalError>
{
    void anchor() override;

public:
    std::string name;

    UndefinedVarError(EvalState & state, std::string name);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * A function was evaluated as a top-level expression without a value
 * for one of its arguments.
 */
class MissingArgumentError : public CloneableError<MissingArgumentError, EvalError>
{
    void anchor() override;

public:
    /**
     * The argument that had no value.
     */
    std::string name;

    MissingArgumentError(EvalState & state, std::string name);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * A value turned out to be of a different type than was needed.
 */
class UnexpectedTypeError : public CloneableError<UnexpectedTypeError, TypeError>
{
    void anchor() override;

public:
    ValueType expected;
    ValueType found;
    /**
     * The offending value, printed plainly.
     */
    std::string value;
    /**
     * The same, printed for the terminal, for the message.
     */
    std::string valueForDisplay;

    UnexpectedTypeError(EvalState & state, ValueType expected, const Value & v);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

class InfiniteRecursionError : public CloneableError<InfiniteRecursionError, EvalError>
{
    void anchor() override;

public:

    /**
     * Memory location of the Value that was found to be a blackhole, used to
     * mark where the recursion starts in the printed trace. Only pointer
     * identity is of interest.
     */
    const Value * const v;

    InfiniteRecursionError(EvalState & state, const Value * v);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * Resource exhaustion error when evaluation exceeds max-call-depth.
 * Inherits from EvalBaseError (not EvalError) because resource exhaustion
 * should not be cached.
 */
class StackOverflowError : public CloneableError<StackOverflowError, EvalBaseError>
{
    void anchor() override;

public:
    StackOverflowError(EvalState & state);

    HintFmt renderMessage() const override;
    std::optional<nlohmann::json> toJSON() const override;
};

MakeError(IFDError, EvalBaseError);

/**
 * An evaluation error which should be retried instead of rethrown.
 *
 * A RecoverableEvalError is not an EvalError, because we shouldn't cache it in
 * the eval cache, as it should be retried anyway.
 */
MakeError(RecoverableEvalError, EvalBaseError);

class InvalidPathError : public CloneableError<InvalidPathError, EvalError>
{
    void anchor() override;

public:
    StorePath path;

    InvalidPathError(EvalState & state, const StorePath & path);

    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * `EvalErrorBuilder`s may only be constructed by `EvalState`. The `debugThrow`
 * method must be the final method in any such `EvalErrorBuilder` usage, and it
 * handles deleting the object.
 */
template<class T>
class EvalErrorBuilder final
{
    friend class EvalState;

    template<typename... Args>
    explicit EvalErrorBuilder(EvalState & state, const Args &... args)
        : error(T(state, args...))
    {
    }

public:
    T error;

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withExitStatus(unsigned int exitStatus);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(PosIdx pos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & atPos(Value & value, PosIdx fallback = noPos);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withTrace(PosIdx pos, const std::string_view text);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withFrameTrace(PosIdx pos, const std::string_view text);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withSuggestions(Suggestions & s);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & withFrame(const Env & e, const Expr & ex);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & addTrace(PosIdx pos, HintFmt hint);

    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> & setIsFromExpr();

    template<typename... Args>
    [[nodiscard, gnu::noinline]] EvalErrorBuilder<T> &
    addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs);

    /**
     * Delete the `EvalErrorBuilder` and throw the underlying exception.
     */
    [[gnu::noinline, gnu::noreturn]] void debugThrow();

    /**
     * A programming error or fatal condition occurred. Abort the process for core dump and debugging.
     * This does not print a proper backtrace, because unwinding the stack is destructive.
     */
    [[gnu::noinline, gnu::noreturn]] void panic();
};

} // namespace nix
