#include "nix/expr/trace-context.hh"
#include "nix/util/util.hh"

#include <nlohmann/json.hpp>

namespace nix {

static std::string ordinal(unsigned n)
{
    switch (n) {
    case 1:
        return "first";
    case 2:
        return "second";
    case 3:
        return "third";
    case 4:
        return "fourth";
    case 5:
        return "fifth";
    case 6:
        return "sixth";
    case 7:
        return "seventh";
    case 8:
        return "eighth";
    case 9:
        return "ninth";
    case 10:
        return "tenth";
    default:
        return std::to_string(n) + "th";
    }
}

bool TraceContext::empty() const
{
    return std::visit(
        overloaded{
            [](std::string_view text) { return text.empty(); },
            [](const BuiltinArgument &) { return false; },
        },
        inner);
}

HintFmt TraceContext::hint() const
{
    return std::visit(
        overloaded{
            [](std::string_view text) { return HintFmt(std::string(text)); },
            [](const BuiltinArgument & a) {
                return HintFmt(
                    "while evaluating the %s argument passed to builtins.%s", ordinal(a.index), a.builtin);
            },
        },
        inner);
}

std::optional<nlohmann::json> TraceContext::data() const
{
    return std::visit(
        overloaded{
            [](std::string_view) -> std::optional<nlohmann::json> { return std::nullopt; },
            [](const BuiltinArgument & a) -> std::optional<nlohmann::json> {
                return nlohmann::json{
                    {"type", "evaluating-builtin-argument"},
                    {"builtin", a.builtin},
                    {"argument", a.index},
                };
            },
        },
        inner);
}

} // namespace nix
