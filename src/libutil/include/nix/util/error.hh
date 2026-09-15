#pragma once
/**
 * @file
 *
 * @brief This file defines two main structs/classes used in nix error handling.
 *
 * ErrorInfo provides a standard payload of error information, with conversion to string
 * happening in the logger rather than at the call site.
 *
 * BaseError is the ancestor of nix specific exceptions (and Interrupted), and contains
 * an ErrorInfo.
 *
 * ErrorInfo structs are sent to the logger as part of an exception, or directly with the
 * logError or logWarning macros.
 * See libutil/tests/logging.cc for usage examples.
 */

#include "nix/util/suggestions.hh"
#include "nix/util/fmt.hh"
#include "nix/util/fun.hh"
#include "nix/util/config.hh"

#include <concepts>
#include <cstring>
#include <list>
#include <memory>
#include <optional>
#include <utility>
#include <algorithm>
#include <concepts>
#include <type_traits>

#include <nlohmann/json_fwd.hpp>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#ifdef _WIN32
#  include <errhandlingapi.h>
#endif

namespace nix {

enum class Verbosity : unsigned {
    lvlError = 0,
    lvlWarn,
    lvlNotice,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit,
};

/* For less churn. */
using enum Verbosity;

template<std::integral T>
    requires(std::is_unsigned_v<T> && !std::is_same_v<bool, T>)
inline Verbosity verbosityFromIntClamped(T val)
{
    /* Clamp, same as https://git.lix.systems/lix-project/lix/commit/f2fff1faa4c6a308ad30da691e18ceccf6626e0d and
       what was effectively the behavior before b9f8c4af4057c2ed0ec5d1ff16ac49e0612ad57c because logErrorInfo
       and friends do a <= nix::verbosity. */
    return static_cast<Verbosity>(std::min<T>(val, std::to_underlying(lvlVomit)));
}

/* So that we don't accidentally run into 2s complement conversion issues.
   Nothing actually needs this conversion. */
template<std::integral T>
    requires std::is_signed_v<T>
Verbosity verbosityFromIntClamped(T) = delete;

/**
 * The lines of code surrounding an error.
 */
struct LinesOfCode
{
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

/* NOTE: position.hh recursively depends on source-path.hh -> source-accessor.hh
   -> hash.hh -> configuration.hh -> experimental-features.hh -> error.hh -> Pos.
   There are other such cycles.
   Thus, Pos has to be an incomplete type in this header. But since ErrorInfo/Trace
   have to refer to Pos, they have to use pointer indirection via std::shared_ptr
   to break the recursive header dependency.
   FIXME: Untangle this mess. Should there be AbstractPos as there used to be before
   4feb7d9f71? */
struct Pos;

void printCodeLines(std::ostream & out, const std::string & prefix, const Pos & errPos, const LinesOfCode & loc);

/**
 * When a stack frame is printed.
 */
enum struct TracePrint {
    /**
     * The default behavior; always printed when `--show-trace` is set.
     */
    Default,
    /** Always printed. Produced by `builtins.addErrorContext`. */
    Always,
};

struct Trace
{
    std::shared_ptr<const Pos> pos;
    HintFmt hint;
    TracePrint print = TracePrint::Default;
    /**
     * The structured content of this frame, if it has any, for the same
     * purpose as `BaseError::toJSON`: so that a machine reader gets the
     * facts and not just the rendered `hint`.
     */
    std::shared_ptr<const nlohmann::json> data;
};

inline std::strong_ordering operator<=>(const Trace & lhs, const Trace & rhs);

/**
 * Everything about an error except its message.
 *
 * The message is deliberately not here, so that errors can produce it
 * on demand (see `BaseError::renderMessage`) rather than store it.
 */
struct ErrorInfo
{
    Verbosity level;
    std::shared_ptr<const Pos> pos;
    std::list<Trace> traces;
    /**
     * Some messages are generated directly by expressions; notably `builtins.warn`, `abort`, `throw`.
     * These may be rendered differently, so that users can distinguish them.
     */
    bool isFromExpr = false;
    bool noIndent = false;

    /**
     * Exit status.
     */
    unsigned int status = 1;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};

/**
 * Render an error in the standard format: prefix, message, position,
 * suggestions, and traces.
 *
 * @param msg Writes the message proper to the given stream. A callback
 * so that a message need not be materialised into a string just to be
 * printed.
 */
std::ostream &
showErrorInfo(std::ostream & out, const ErrorInfo & einfo, fun<void(std::ostream &)> msg, bool showTrace);

/**
 * Convenience for the common case of an already-formatted message.
 */
std::ostream & showErrorInfo(std::ostream & out, const ErrorInfo & einfo, const HintFmt & msg, bool showTrace);

/**
 * `ErrorInfo` as it was before the message was split out of it.
 *
 * This exists only so that the existing `throw UnstructuredError({.msg = ..., .pos = ...})`
 * constructions keep working, via `Unstructured(ErrorInfoCompat &&)`. New code
 * should pass the `ErrorInfo` and the message separately, and once all the
 * old sites are converted this should go away.
 */
struct ErrorInfoCompat
{
    Verbosity level;
    HintFmt msg;
    std::shared_ptr<const Pos> pos;
    std::list<Trace> traces;
    bool isFromExpr = false;
    bool noIndent = false;
    unsigned int status = 1;
    Suggestions suggestions;
};

/**
 * The root of the exception hierarchy.
 *
 * Holds the `ErrorInfo`, but not the message: that is produced by the
 * virtual `renderMessage`. Structured errors thus keep their data in
 * fields and format the message from them on demand, rather than
 * storing a format string that duplicates the fields (see issue #7865).
 * Ad hoc errors, made with a format string at the throw site, get that
 * string stored for them by the `Unstructured` mixin.
 *
 * Should generally not be caught, as it has `Interrupted` as a subclass.
 * Catch `Error` instead.
 */
class BaseError : public std::exception
{
    /* VTable anchor to avoid weak linkage of the vtable - it breaks
       dynamic_cast across shared libraries on Darwin. */
    virtual void anchor();

protected:
    mutable ErrorInfo err;

    /**
     * Cached result of rendering the whole error, see `calcWhat`.
     */
    mutable std::optional<std::string> what_;
    /**
     * Render the whole error, as `showErrorInfo` would, and set `what_`
     * to the resulting value.
     */
    const std::string & calcWhat() const;

    BaseError(const BaseError &) = default;
    BaseError & operator=(const BaseError &) = default;
    BaseError & operator=(BaseError &&) noexcept = default;

    BaseError(ErrorInfo && e)
        : err(std::move(e))
    {
    }

    BaseError(const ErrorInfo & e)
        : err(e)
    {
    }

public:
    /**
     * The message proper: without the "error: " prefix, position,
     * suggestions, or traces.
     */
    virtual HintFmt renderMessage() const = 0;

    /** The error message without "error: " prefixed to it. */
    std::string message() const
    {
        return renderMessage().str();
    }

    /**
     * The structured content of the error, if it has any, for machine
     * consumption: the JSON logger emits it alongside the rendered
     * message. Errors with fields should override this to expose them;
     * unstructured errors have nothing to offer beyond the message.
     */
    virtual std::optional<nlohmann::json> toJSON() const;

    const char * what() const noexcept override
    {
        return calcWhat().c_str();
    }

    const std::string & msg() const
    {
        return calcWhat();
    }

    const ErrorInfo & info() const
    {
        calcWhat();
        return err;
    }

    void withExitStatus(unsigned int status)
    {
        err.status = status;
    }

    void atPos(std::shared_ptr<const Pos> pos)
    {
        err.pos = pos;
    }

    bool hasPos() const;

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    /**
     * Prepends an item to the error trace, as is usual for extra context.
     *
     * @param pos Nullable source position to put in trace item
     * @param fs Format string, see `HintFmt`
     * @param args Format string arguments.
     */
    template<typename... Args>
    void addTrace(std::shared_ptr<const Pos> && pos, std::string_view fs, Args &&... args)
    {
        addTrace(std::move(pos), HintFmt(std::string(fs), std::forward<Args>(args)...));
    }

    /**
     * Prepends an item to the error trace, as is usual for extra context.
     *
     * @param pos Nullable source position to put in trace item
     * @param hint Formatted error message
     * @param print Optional, whether to always print (used by `addErrorContext`)
     */
    void addTrace(std::shared_ptr<const Pos> && pos, HintFmt hint, TracePrint print = TracePrint::Default);

    /**
     * Prepends an item to the error trace, with structured content as
     * well as the rendered hint. See `Trace::data`.
     */
    void addTrace(
        std::shared_ptr<const Pos> && pos, HintFmt hint, nlohmann::json data, TracePrint print = TracePrint::Default);

    /**
     * Prepends an item to the error trace from anything that can render a
     * hint and, optionally, structured data for it, such as the
     * evaluator's `TraceContext`.
     */
    template<typename Ctx>
        requires requires(const Ctx & c) {
            { c.hint() } -> std::convertible_to<HintFmt>;
            { c.data() } -> std::convertible_to<std::optional<nlohmann::json>>;
        }
    void addTrace(std::shared_ptr<const Pos> && pos, const Ctx & ctx)
    {
        if (auto data = ctx.data())
            addTrace(std::move(pos), ctx.hint(), std::move(*data));
        else
            addTrace(std::move(pos), ctx.hint());
    }

    bool hasTrace() const
    {
        return !err.traces.empty();
    }

    /**
     * Returns a mutable reference to the error info.
     *
     * @warning After modifying the returned ErrorInfo, you must call
     * recalcWhat() to update the cached formatted message.
     */
    ErrorInfo & unsafeInfo()
    {
        return err;
    }

    /**
     * Recalculate the cached formatted error message.
     * Must be called after modifying the error info via unsafeInfo().
     */
    void recalcWhat() const;

    [[noreturn]] virtual void throwClone() const = 0;
};

template<typename Derived, typename Base>
class CloneableError : public Base
{
    friend Derived;
    CloneableError() = default;
    using Base::Base;
public:

    /**
     * Rethrow a copy of this exception. Useful when the exception can get
     * modified when appending traces.
     */
    [[noreturn]] void throwClone() const override
    {
        throw Derived(static_cast<const Derived &>(*this));
    }
};

#define MakeError(newClass, superClass)                             \
    class newClass : public CloneableError<newClass, superClass>    \
    {                                                               \
        void anchor() override;                                     \
    public:                                                         \
        using CloneableError<newClass, superClass>::CloneableError; \
    }

/**
 * The error type to catch.
 *
 * Abstract: it has no message of its own. Every concrete error is
 * either an `UnstructuredError` (or subclass), which stores a formatted
 * message, or a structured error, which derives from this (or a
 * subclass) and implements `renderMessage` in terms of its own fields.
 * Either way, `catch (Error &)` gets it.
 */
class Error : public BaseError
{
    void anchor() override;

protected:
    using BaseError::BaseError;
};

/**
 * Mixin that gives an error a pre-formatted message.
 *
 * This is what the vast majority of errors are: made ad hoc at the throw
 * site from a format string and arguments. It provides all the
 * constructors that take a format string, and stores the resulting
 * `HintFmt` as `hint`, which is what `renderMessage` returns.
 *
 * `Base` is the abstract error class to hang the message off of:
 * `Error` for `UnstructuredError`, `BaseError` for `Interrupted`.
 */
template<typename Base>
class Unstructured : public Base
{
protected:
    /**
     * For structured errors that derive from an unstructured one: they
     * override `renderMessage`, so have no use for `hint`, but must still
     * pass through here on the way to `Base`. The tag keeps this from
     * competing with the constructors below in overload resolution.
     */
    struct NoHint
    {};

    Unstructured(NoHint, ErrorInfo && e)
        : Base(std::move(e))
        , hint("")
    {
    }

public:
    /**
     * The pre-formatted message.
     */
    HintFmt hint;

    Unstructured(const Unstructured &) = default;
    Unstructured & operator=(const Unstructured &) = default;
    Unstructured & operator=(Unstructured &&) noexcept = default;

    template<typename... Args>
    Unstructured(unsigned int status, Args &&... args)
        : Base(ErrorInfo{.level = lvlError, .status = status})
        , hint(std::forward<Args>(args)...)
    {
    }

    template<typename... Args>
    explicit Unstructured(const std::string & fs, Args &&... args)
        : Base(ErrorInfo{.level = lvlError})
        , hint(fs, std::forward<Args>(args)...)
    {
    }

    template<typename... Args>
    Unstructured(const Suggestions & sug, Args &&... args)
        : Base(ErrorInfo{.level = lvlError, .suggestions = sug})
        , hint(std::forward<Args>(args)...)
    {
    }

    Unstructured(HintFmt hint)
        : Base(ErrorInfo{.level = lvlError})
        , hint(std::move(hint))
    {
    }

    Unstructured(ErrorInfo && e, HintFmt hint)
        : Base(std::move(e))
        , hint(std::move(hint))
    {
    }

    Unstructured(const ErrorInfo & e, HintFmt hint)
        : Base(e)
        , hint(std::move(hint))
    {
    }

    /**
     * Deprecated: for the many existing `throw UnstructuredError({.msg = ..., .pos = ...})`
     * sites. Prefer the two-argument constructor above.
     */
    Unstructured(ErrorInfoCompat && e)
        : Base(
              ErrorInfo{
                  .level = e.level,
                  .pos = std::move(e.pos),
                  .traces = std::move(e.traces),
                  .isFromExpr = e.isFromExpr,
                  .noIndent = e.noIndent,
                  .status = e.status,
                  .suggestions = std::move(e.suggestions),
              })
        , hint(std::move(e.msg))
    {
    }

    HintFmt renderMessage() const override
    {
        return hint;
    }
};

/**
 * The error type to throw when there is nothing structured to say: just
 * a message made from a format string at the throw site.
 */
MakeError(UnstructuredError, Unstructured<Error>);

/**
 * An error received from another process. Its message arrived as a
 * string, and its structured content, if any, as whatever JSON the
 * sender's `toJSON` produced, which `toJSON` here hands on unchanged.
 * See `readError`.
 */
class RemoteError : public CloneableError<RemoteError, UnstructuredError>
{
    void anchor() override;

public:
    std::shared_ptr<const nlohmann::json> structured;

    RemoteError(ErrorInfo && info, HintFmt hint, std::shared_ptr<const nlohmann::json> structured)
        : CloneableError(std::move(info), std::move(hint))
        , structured(std::move(structured))
    {
    }

    std::optional<nlohmann::json> toJSON() const override;
};

MakeError(UsageError, UnstructuredError);
MakeError(UnimplementedError, UnstructuredError);

/**
 * To use in catch-blocks. Provides a convenience method to get the portable
 * std::error_code. Use when you want to catch and check an error condition like
 * no_such_file_or_directory (ENOENT) without ifdefs.
 */
class SystemError : public CloneableError<SystemError, UnstructuredError>
{
    std::error_code errorCode;
    std::string errorDetails;

    void anchor() override;

protected:

    /**
     * Just here to allow derived classes to use the right constructor
     * (the protected one).
     *
     * This one indicates the prebuilt `HintFmt` one with the explicit `errorDetails`
     */
    struct DisambigHintFmt
    {};

    /**
     * Just here to allow derived classes to use the right constructor
     * (the protected one).
     *
     * This one indicates the varargs one to build the `HintFmt` with the explicit `errorDetails`
     */
    struct DisambigVarArgs
    {};

    /**
     * Protected constructor that takes a pre-built HintFmt.
     * Use this when the error message needs to be constructed before
     * capturing errno/GetLastError().
     */
    SystemError(DisambigHintFmt, std::error_code errorCode, std::string_view errorDetails, const HintFmt & hf)
        : CloneableError(HintFmt{"%s: %s", Uncolored(hf.str()), errorDetails})
        , errorCode(errorCode)
        , errorDetails(errorDetails)
    {
    }

    /**
     * Protected constructor for subclasses that provide their own error message.
     * The error message is appended to the formatted hint.
     */
    template<typename... Args>
    SystemError(DisambigVarArgs, std::error_code errorCode, std::string_view errorDetails, Args &&... args)
        : SystemError(DisambigHintFmt{}, errorCode, errorDetails, HintFmt{std::forward<Args>(args)...})
    {
    }

public:
    /**
     * Construct with an error code. The error code's message is automatically
     * appended to the error message.
     */
    SystemError(std::error_code errorCode, const HintFmt & hf)
        : SystemError(DisambigHintFmt{}, errorCode, errorCode.message(), hf)
    {
    }

    /**
     * Construct with an error code. The error code's message is automatically
     * appended to the error message.
     */
    template<typename... Args>
    SystemError(std::error_code errorCode, Args &&... args)
        : SystemError(DisambigVarArgs{}, errorCode, errorCode.message(), std::forward<Args>(args)...)
    {
    }

    const std::error_code ec() const &
    {
        return errorCode;
    }

    bool is(std::errc e) const
    {
        return errorCode == e;
    }

    std::optional<nlohmann::json> toJSON() const override;
};

/**
 * POSIX system error, created using `errno`, `strerror` friends.
 *
 * Throw this, but prefer not to catch this, and catch `SystemError`
 * instead. This allows implementations to freely switch between this
 * and `windows::WinError` without breaking catch blocks.
 *
 * However, it is permissible to catch this and rethrow so long as
 * certain conditions are not met (e.g. to catch only if `errNo =
 * EFooBar`). In that case, try to also catch the equivalent `windows::WinError`
 * code.
 *
 * @todo Rename this to `PosixError` or similar. At this point Windows
 * support is too WIP to justify the code churn, but if it is finished
 * then a better identifier becomes moe worth it.
 */
class SysError final : public CloneableError<SysError, SystemError>
{
    void anchor() override;

public:
    int errNo;

    /**
     * Construct using the explicitly-provided error number. `strerror`
     * will be used to try to add additional information to the message.
     */
    template<typename... Args>
    SysError(int errNo, Args &&... args)
        : CloneableError(
              DisambigVarArgs{},
              std::make_error_code(static_cast<std::errc>(errNo)),
              strerror(errNo),
              std::forward<Args>(args)...)
        , errNo(errNo)
    {
    }

    /**
     * Construct using the explicitly-provided error number. `strerror`
     * will be used to try to add additional information to the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    SysError(int errNo, const HintFmt & hf)
        : CloneableError(DisambigHintFmt{}, std::make_error_code(static_cast<std::errc>(errNo)), strerror(errNo), hf)
        , errNo(errNo)
    {
    }

    /**
     * Construct using the ambient `errno`.
     *
     * Be sure to not perform another `errno`-modifying operation before
     * calling this constructor!
     */
    template<typename... Args>
    SysError(Args &&... args)
        : SysError(errno, std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using the ambient `errno` and a function that produces
     * a `HintFmt`. errno is read first, then the function is called, so
     * the function is safe to modify `errno`.
     */
    SysError(auto && mkHintFmt)
        requires std::invocable<decltype(mkHintFmt)> && std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
        : SysError(captureErrno(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
    {
    }

private:
    /**
     * Helper to ensure errno is captured before mkHintFmt is called.
     * C++ argument evaluation order is unspecified, so we can't rely on
     * `SysError(errno, mkHintFmt())` evaluating errno first.
     */
    static std::pair<int, HintFmt> captureErrno(auto && mkHintFmt)
    {
        int e = errno;
        return {e, mkHintFmt()};
    }

    SysError(std::pair<int, HintFmt> && p)
        : SysError(p.first, std::move(p.second))
    {
    }
};

/**
 * Throw an exception for the purpose of checking that exception
 * handling works; see 'initLibUtil()'.
 */
void throwExceptionSelfCheck();

/**
 * Print a message and std::terminate().
 */
[[noreturn]]
void panic(std::string_view msg);

/**
 * Run a function, printing an error and returning on exception.
 * Useful for wrapping a `main` function that may throw
 *
 * @param programName Name of program, usually argv[0]
 * @param body Function to run inside the try block
 * @return exit code: 0 if success, 1 if exception does not specify.
 */
int handleExceptions(const std::string & programName, fun<void()> body);

/**
 * Print a basic error message with source position and std::terminate().
 *
 * @note: This assumes that the logger is operational
 */
[[gnu::noinline, gnu::cold, noreturn]] void unreachable(std::source_location loc = std::source_location::current());

#if NIX_UBSAN_ENABLED
/* When building with sanitizers, also enable expensive unreachable checks. In
   optimised builds this explicitly invokes UB with std::unreachable for better
   optimisations. */
#  define nixUnreachableWhenHardened ::nix::unreachable
#else
#  define nixUnreachableWhenHardened std::unreachable
#endif

#ifdef _WIN32

namespace windows {

/**
 * Windows Error type.
 *
 * Unless you need to catch a specific error number, don't catch this in
 * portable code. Catch `SystemError` instead.
 */
class WinError : public CloneableError<WinError, SystemError>
{
    void anchor() override;

public:
    DWORD lastError;

    /**
     * Construct using the explicitly-provided error number.
     * `FormatMessageA` will be used to try to add additional
     * information to the message.
     */
    template<typename... Args>
    WinError(DWORD lastError, Args &&... args)
        : CloneableError(
              DisambigVarArgs{},
              std::error_code(lastError, std::system_category()),
              renderError(lastError),
              std::forward<Args>(args)...)
        , lastError(lastError)
    {
    }

    /**
     * Construct using the explicitly-provided error number.
     * `FormatMessageA` will be used to try to add additional
     * information to the message.
     *
     * Unlike above, the `HintFmt` already exists rather than being made on
     * the spot.
     */
    WinError(DWORD lastError, const HintFmt & hf)
        : CloneableError(
              DisambigHintFmt{}, std::error_code(lastError, std::system_category()), renderError(lastError), hf)
        , lastError(lastError)
    {
    }

    /**
     * Construct using `GetLastError()` and the ambient "last error".
     *
     * Be sure to not perform another last-error-modifying operation
     * before calling this constructor!
     */
    template<typename... Args>
    WinError(Args &&... args)
        : WinError(GetLastError(), std::forward<Args>(args)...)
    {
    }

    /**
     * Construct using `GetLastError()` and a function that produces a
     * `HintFmt`. `GetLastError()` is called first, then the function is
     * called, so the function is safe to modify the last error.
     */
    WinError(auto && mkHintFmt)
        requires std::invocable<decltype(mkHintFmt)> && std::same_as<std::invoke_result_t<decltype(mkHintFmt)>, HintFmt>
        : WinError(captureLastError(std::forward<decltype(mkHintFmt)>(mkHintFmt)))
    {
    }

private:
    /**
     * Helper to ensure GetLastError() is captured before mkHintFmt is called.
     * C++ argument evaluation order is unspecified, so we can't rely on
     * `WinError(GetLastError(), mkHintFmt())` evaluating GetLastError() first.
     */
    static std::pair<DWORD, HintFmt> captureLastError(auto && mkHintFmt)
    {
        DWORD e = GetLastError();
        return {e, mkHintFmt()};
    }

    WinError(std::pair<DWORD, HintFmt> && p)
        : WinError(p.first, std::move(p.second))
    {
    }

    static std::string renderError(DWORD lastError);
};

} // namespace windows

#endif

/**
 * Convenience alias for when we use a `errno`-based error handling
 * function on Unix, and `GetLastError()`-based error handling on on
 * Windows.
 */
using NativeSysError =
#ifdef _WIN32
    windows::WinError
#else
    SysError
#endif
    ;

} // namespace nix
