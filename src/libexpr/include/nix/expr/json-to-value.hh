#pragma once
///@file

#include "nix/util/error.hh"

#include <string>

namespace nix {

class EvalState;
struct Value;

MakeError(JSONParseError, UnstructuredError);

void parseJSON(EvalState & state, const std::string_view & s, Value & v);

} // namespace nix
