#pragma once
///@file

#include "nix/util/error.hh"

#include <string>
#include <string_view>
#include <variant>

namespace nix {

/**
 * The context in which the evaluator is doing something, to become a
 * trace frame if that something fails. Passed as `errorCtx` to the
 * `EvalState` methods that force or coerce values.
 *
 * Cheap to make and to pass, since nearly every one of them is made on
 * a path that does not fail: nothing is formatted until an error is
 * actually caught. Most are still plain text, which yields a frame with
 * a rendered hint only; the structured shapes yield a frame with data
 * too (see `Trace::data`), and text is meant to move to those over time.
 */
struct TraceContext
{
    /**
     * builtinArgument("foo", 1).
     */
    struct BuiltinArgument
    {
        std::string_view builtin;
        /**
         * 1-based.
         */
        unsigned index;
    };

    /**
     * "in the left operand of '&&'".
     */
    struct Operand
    {
        std::string_view op;

        enum Side { Left, Right } side;
    };

    /**
     * "while evaluating the 'name' attribute of a derivation".
     */
    struct DerivationAttribute
    {
        std::string_view attribute;
    };

    /**
     * "while evaluating the attribute 'hash'".
     */
    struct Attribute
    {
        std::string_view attribute;
    };

    std::variant<std::string_view, BuiltinArgument, Operand, DerivationAttribute, Attribute> inner;

    TraceContext(std::string_view text)
        : inner(text)
    {
    }

    TraceContext(const char * text)
        : inner(std::string_view(text))
    {
    }

    /**
     * Refers to the string, as the `std::string_view` one does; the
     * string must outlive the call this is the context of, which a
     * temporary made at the call site does.
     */
    TraceContext(const std::string & text)
        : inner(std::string_view(text))
    {
    }

    TraceContext(BuiltinArgument argument)
        : inner(argument)
    {
    }

    TraceContext(Operand operand)
        : inner(operand)
    {
    }

    TraceContext(DerivationAttribute attribute)
        : inner(attribute)
    {
    }

    TraceContext(Attribute attribute)
        : inner(attribute)
    {
    }

    /**
     * Whether there is nothing here. Some callers pass `""` to mean that.
     */
    bool empty() const;

    HintFmt hint() const;

    std::optional<nlohmann::json> data() const;
};

/**
 * The context for evaluating the `index`th (1-based) argument of the
 * builtin called `name`.
 */
inline TraceContext builtinArgument(std::string_view name, unsigned index)
{
    return TraceContext::BuiltinArgument{.builtin = name, .index = index};
}

inline TraceContext leftOperand(std::string_view op)
{
    return TraceContext::Operand{.op = op, .side = TraceContext::Operand::Left};
}

inline TraceContext rightOperand(std::string_view op)
{
    return TraceContext::Operand{.op = op, .side = TraceContext::Operand::Right};
}

inline TraceContext derivationAttribute(std::string_view attribute)
{
    return TraceContext::DerivationAttribute{.attribute = attribute};
}

inline TraceContext evaluatingAttribute(std::string_view attribute)
{
    return TraceContext::Attribute{.attribute = attribute};
}

} // namespace nix
