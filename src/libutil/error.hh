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

#include "suggestions.hh"
#include "ref.hh"
#include "types.hh"
#include "fmt.hh"

#include <cstring>
#include <list>
#include <memory>
#include <map>
#include <optional>

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

/* Before 4.7, gcc's std::exception uses empty throw() specifiers for
 * its (virtual) destructor and what() in c++11 mode, in violation of spec
 */
#ifdef __GNUC__
#if __GNUC__ < 4 || (__GNUC__ == 4 && __GNUC_MINOR__ < 7)
#define EXCEPTION_NEEDS_THROW_SPEC
#endif
#endif

namespace nix {


typedef enum {
    lvlError = 0,
    lvlWarn,
    lvlNotice,
    lvlInfo,
    lvlTalkative,
    lvlChatty,
    lvlDebug,
    lvlVomit
} Verbosity;

/**
 * The lines of code surrounding an error.
 */
struct LinesOfCode {
    std::optional<std::string> prevLineOfCode;
    std::optional<std::string> errLineOfCode;
    std::optional<std::string> nextLineOfCode;
};

/**
 * An abstract type that represents a location in a source file.
 */
struct AbstractPos
{
    uint32_t line = 0;
    uint32_t column = 0;

    /**
     * Return the contents of the source file.
     */
    virtual std::optional<std::string> getSource() const
    { return std::nullopt; };

    virtual void print(std::ostream & out) const = 0;

    std::optional<LinesOfCode> getCodeLines() const;

    virtual ~AbstractPos() = default;
};

std::ostream & operator << (std::ostream & str, const AbstractPos & pos);

void printCodeLines(std::ostream & out,
    const std::string & prefix,
    const AbstractPos & errPos,
    const LinesOfCode & loc);

struct Trace {
    std::shared_ptr<AbstractPos> pos;
    hintformat hint;
    bool frame;
};

struct ErrorInfo {
    Verbosity level;
    std::shared_ptr<AbstractPos> errPos;
    std::list<Trace> traces;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};

std::ostream & showErrorInfo(
    std::ostream & out,
    const ErrorInfo & einfo,
    std::function<void(std::ostringstream & oss)> msg,
    bool showTrace);
/* Convenience for the common case. */
std::ostream & showErrorInfo(
    std::ostream & out,
    const ErrorInfo & einfo,
    hintformat msg,
    bool showTrace);

class Base0Error : public std::exception
{
protected:
    mutable ErrorInfo err;

    mutable std::optional<std::string> what_;
    const std::string & calcWhat() const;
    virtual std::string calcWhatUncached() const = 0;

    Base0Error(const Base0Error &) = default;

    Base0Error(unsigned int status, ErrorInfo && e)
        : err(std::move(e))
        , status(status)
    { }

    Base0Error(ErrorInfo && e)
        : err(std::move(e))
    { }

    Base0Error(const ErrorInfo & e)
        : err(e)
    { }

public:
    unsigned int status = 1; // exit status

#ifdef EXCEPTION_NEEDS_THROW_SPEC
    ~Base0Error() throw () { };
    const char * what() const throw () { return calcWhat().c_str(); }
#else
    const char * what() const noexcept override { return calcWhat().c_str(); }
#endif

    const std::string & msg() const { return calcWhat(); }
    const ErrorInfo & info() const { calcWhat(); return err; }

    void pushTrace(Trace trace)
    {
        err.traces.push_front(trace);
    }

    template<typename... Args>
    void addTrace(std::shared_ptr<AbstractPos> && e, std::string_view fs, const Args & ... args)
    {
        addTrace(std::move(e), hintfmt(std::string(fs), args...));
    }

    void addTrace(std::shared_ptr<AbstractPos> && e, hintformat hint, bool frame = false);

    bool hasTrace() const { return !err.traces.empty(); }

    const ErrorInfo & info() { return err; };
};


/**
 * The old version of `ErrorInfo`, with a message.
 *
 * This is deprecated.
 *
 * This just exists for the sake of existing constructions of
 * `BaseError` and derived types. Once those are all converted to
 * something else (e.g. structured) we should get rid of this.
 */
struct ErrorInfoCompat {
    Verbosity level;
    hintformat msg;
    std::shared_ptr<AbstractPos> errPos;
    std::list<Trace> traces;

    Suggestions suggestions;

    static std::optional<std::string> programName;
};


/**
 * Base0Error should generally not be caught, as it has `Interrupted` as
 * a subclass. Catch `Error` instead.
 */
struct BaseError : Base0Error
{
    hintformat message;

    /**
     * Deprecated.
     */
    BaseError(ErrorInfoCompat && e)
        : Base0Error(ErrorInfo {
            .level = e.level,
            .errPos = e.errPos,
            .traces = e.traces,
            .suggestions = e.suggestions,
        })
        , message(e.msg)
    { }

    BaseError(ErrorInfo && e, hintformat && message)
        : Base0Error(e), message(message)
    { }

    BaseError(const ErrorInfo & e, const hintformat & message)
        : Base0Error(e), message(message)
    { }

    template<typename... Args>
    BaseError(unsigned int status, const Args & ... args)
        : Base0Error(status, ErrorInfo { .level = lvlError })
        , message(hintfmt(args...))
    { }

    template<typename... Args>
    explicit BaseError(const std::string & fs, const Args & ... args)
        : Base0Error(ErrorInfo { .level = lvlError })
        , message(hintfmt(fs, args...))
    { }

    template<typename... Args>
    BaseError(const Suggestions & sug, const Args & ... args)
        : Base0Error(ErrorInfo { .level = lvlError, .suggestions = sug })
        , message(hintfmt(args...))
    { }

    BaseError(hintformat hint)
        : Base0Error(ErrorInfo { .level = lvlError })
        , message(hint)
    { }

    std::string calcWhatUncached() const override;
};

#define MakeError(newClass, superClass) \
    class newClass : public superClass                  \
    {                                                   \
    public:                                             \
        using superClass::superClass;                   \
    }

MakeError(Error, BaseError);
MakeError(UsageError, Error);
MakeError(UnimplementedError, Error);

class SysError : public Error
{
public:
    int errNo;

    template<typename... Args>
    SysError(int errNo_, const Args & ... args)
        : Error("")
    {
        errNo = errNo_;
        auto hf = hintfmt(args...);
        message = hintfmt("%1%: %2%", normaltxt(hf.str()), strerror(errNo));
    }

    template<typename... Args>
    SysError(const Args & ... args)
        : SysError(errno, args ...)
    {
    }
};

}
