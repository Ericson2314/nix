#include "nix/expr/eval-error.hh"
#include "nix/expr/eval.hh"
#include "nix/expr/value.hh"
#include "nix/store/store-api.hh"

#include "nix/expr/print.hh"
#include <nlohmann/json.hpp>

namespace nix {

InvalidPathError::InvalidPathError(EvalState & state, const StorePath & path)
    : CloneableError(state, "path '%s' is not valid", state.store->printStorePath(path))
    , path{path}
{
}

std::optional<nlohmann::json> InvalidPathError::toJSON() const
{
    return nlohmann::json{{"type", "invalid-path"}, {"path", state.store->printStorePath(path)}};
}

ThrownError::ThrownError(EvalState & state, std::string text)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , text(std::move(text))
{
}

HintFmt ThrownError::renderMessage() const
{
    return HintFmt(text);
}

std::optional<nlohmann::json> ThrownError::toJSON() const
{
    return nlohmann::json{{"type", "throw"}, {"message", text}};
}

AssertFailedError::AssertFailedError(EvalState & state, std::string expression)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , expression(std::move(expression))
{
}

HintFmt AssertFailedError::renderMessage() const
{
    return HintFmt("assertion '%1%' failed", expression);
}

std::optional<nlohmann::json> AssertFailedError::toJSON() const
{
    return nlohmann::json{{"type", "assertion-failed"}, {"expression", expression}};
}

Abort::Abort(EvalState & state, std::string text)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , text(std::move(text))
{
}

HintFmt Abort::renderMessage() const
{
    return HintFmt("evaluation aborted with the following error message: '%1%'", text);
}

std::optional<nlohmann::json> Abort::toJSON() const
{
    return nlohmann::json{{"type", "abort"}, {"message", text}};
}

UndefinedVarError::UndefinedVarError(EvalState & state, std::string name)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , name(std::move(name))
{
}

HintFmt UndefinedVarError::renderMessage() const
{
    return HintFmt("undefined variable '%1%'", name);
}

std::optional<nlohmann::json> UndefinedVarError::toJSON() const
{
    return nlohmann::json{{"type", "undefined-variable"}, {"name", name}};
}

MissingArgumentError::MissingArgumentError(EvalState & state, std::string name)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , name(std::move(name))
{
}

HintFmt MissingArgumentError::renderMessage() const
{
    return HintFmt(
        R"(cannot evaluate a function that has an argument without a value ('%1%')
Nix attempted to evaluate a function as a top level expression; in
this case it must have its arguments supplied either by default
values, or passed explicitly with '--arg' or '--argstr'. See
https://nix.dev/manual/nix/stable/language/syntax.html#functions.)",
        name);
}

std::optional<nlohmann::json> MissingArgumentError::toJSON() const
{
    return nlohmann::json{{"type", "missing-argument"}, {"name", name}};
}

UnexpectedTypeError::UnexpectedTypeError(EvalState & state, ValueType expected, const Value & v)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , expected(expected)
    , found(v.type())
{
    /* `ValuePrinter` takes a mutable reference, though with these options
       it does not force anything. */
    auto print = [&](PrintOptions options) {
        std::ostringstream oss;
        oss << ValuePrinter(state, const_cast<Value &>(v), options);
        return oss.str();
    };
    auto plainOptions = errorPrintOptions;
    plainOptions.ansiColors = false;
    value = print(plainOptions);
    valueForDisplay = print(errorPrintOptions);
}

HintFmt UnexpectedTypeError::renderMessage() const
{
    return HintFmt("expected %1% but found %2%: %3%", showType(expected), showType(found), valueForDisplay);
}

std::optional<nlohmann::json> UnexpectedTypeError::toJSON() const
{
    return nlohmann::json{
        {"type", "unexpected-type"},
        {"expected", showType(expected, false)},
        {"found", showType(found, false)},
        {"value", value},
    };
}

InfiniteRecursionError::InfiniteRecursionError(EvalState & state, const Value * v)
    : CloneableError(state, ErrorInfo{.level = lvlError})
    , v(v)
{
}

HintFmt InfiniteRecursionError::renderMessage() const
{
    return HintFmt("infinite recursion encountered");
}

std::optional<nlohmann::json> InfiniteRecursionError::toJSON() const
{
    return nlohmann::json{{"type", "infinite-recursion"}};
}

StackOverflowError::StackOverflowError(EvalState & state)
    : CloneableError(state, ErrorInfo{.level = lvlError})
{
}

HintFmt StackOverflowError::renderMessage() const
{
    return HintFmt("stack overflow; max-call-depth exceeded");
}

std::optional<nlohmann::json> StackOverflowError::toJSON() const
{
    return nlohmann::json{{"type", "stack-overflow"}};
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withExitStatus(unsigned int exitStatus)
{
    error.withExitStatus(exitStatus);
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::atPos(PosIdx pos)
{
    error.err.pos = error.state.positions[pos];
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::atPos(Value & value, PosIdx fallback)
{
    return atPos(value.determinePos(fallback));
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withTrace(PosIdx pos, const TraceContext & ctx)
{
    error.addTrace(error.state.positions[pos], ctx);
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withSuggestions(Suggestions & s)
{
    error.err.suggestions = s;
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::withFrame(const Env & env, const Expr & expr)
{
    // NOTE: This is abusing side-effects.
    // TODO: check compatibility with nested debugger calls.
    // TODO: What side-effects??
    error.state.debugTraces.push_front(
        DebugTrace{
            .pos = expr.getPos(),
            .expr = expr,
            .env = env,
            .hint = HintFmt("Fake frame for debugging purposes"),
            .isError = true});
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::addTrace(PosIdx pos, HintFmt hint)
{
    error.addTrace(error.state.positions[pos], hint);
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::addTrace(PosIdx pos, HintFmt hint, nlohmann::json data)
{
    error.addTrace(error.state.positions[pos], std::move(hint), std::move(data));
    return *this;
}

template<class T>
template<typename... Args>
EvalErrorBuilder<T> &
EvalErrorBuilder<T>::addTrace(PosIdx pos, std::string_view formatString, const Args &... formatArgs)
{

    addTrace(error.state.positions[pos], HintFmt(std::string(formatString), formatArgs...));
    return *this;
}

template<class T>
EvalErrorBuilder<T> & EvalErrorBuilder<T>::setIsFromExpr()
{
    error.err.isFromExpr = true;
    return *this;
}

template<class T>
void EvalErrorBuilder<T>::debugThrow()
{
    error.state.runDebugRepl(&error);

    // `EvalState` is the only class that can construct an `EvalErrorBuilder`,
    // and it does so in dynamic storage. This is the final method called on
    // any such instance and must delete itself before throwing the underlying
    // error.
    auto error = std::move(this->error);
    delete this;

    throw std::move(error);
}

template<class T>
void EvalErrorBuilder<T>::panic()
{
    logExError(error);
    printError(
        "This is a bug! An unexpected condition occurred, causing the Nix evaluator to have to stop. If you could share a reproducible example or a core dump, please open an issue at https://github.com/NixOS/nix/issues");
    abort();
}

template class EvalErrorBuilder<EvalBaseError>;
template class EvalErrorBuilder<EvalError>;
template class EvalErrorBuilder<AssertionError>;
template class EvalErrorBuilder<AssertFailedError>;
template class EvalErrorBuilder<ThrownError>;
template class EvalErrorBuilder<Abort>;
template class EvalErrorBuilder<TypeError>;
template class EvalErrorBuilder<UnexpectedTypeError>;
template class EvalErrorBuilder<UndefinedVarError>;
template class EvalErrorBuilder<MissingArgumentError>;
template class EvalErrorBuilder<InfiniteRecursionError>;
template class EvalErrorBuilder<StackOverflowError>;
template class EvalErrorBuilder<InvalidPathError>;
template class EvalErrorBuilder<IFDError>;
template class EvalErrorBuilder<RecoverableEvalError>;

void EvalBaseError::anchor() {}

void ParseError::anchor() {}

void EvalError::anchor() {}

void AssertionError::anchor() {}

void AssertFailedError::anchor() {}

void ThrownError::anchor() {}

void Abort::anchor() {}

void TypeError::anchor() {}

void UnexpectedTypeError::anchor() {}

void UndefinedVarError::anchor() {}

void MissingArgumentError::anchor() {}

void InfiniteRecursionError::anchor() {}

void StackOverflowError::anchor() {}

void InvalidPathError::anchor() {}

void IFDError::anchor() {}

void RecoverableEvalError::anchor() {}

} // namespace nix
