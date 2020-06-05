#include "command.hh"
#include "common-args.hh"
#include "shared.hh"
#include "store-api.hh"
#include "eval.hh"
#include "json.hh"
#include "value-to-json.hh"

using namespace nix;

struct CmdEval : MixJSON, InstallablesCommand
{
    bool raw = false;

    CmdEval()
    {
        mkFlag(0, "raw", "print strings unquoted", &raw);
    }

    std::string name() override
    {
        return "eval";
    }

    std::string description() override
    {
        return "evaluate a Nix expression";
    }

    void run(ref<Store> store) override
    {
        if (raw && json)
            throw UsageError("--raw and --json are mutually exclusive");

        auto state = getEvalState();

        auto jsonOut = json ? std::make_unique<JSONList>(std::cout) : nullptr;

        for (auto & i : installables) {
            auto v = i->toValue(*state);
            if (raw) {
                std::cout << state->forceString(*v);
            } else if (json) {
                PathSet context;
                auto jsonElem = jsonOut->placeholder();
                printValueAsJSON(*state, true, *v, jsonElem, context);
            } else {
                state->forceValueDeep(*v);
                std::cout << *v << "\n";
            }
        }
    }
};

static RegisterCommand r1(make_ref<CmdEval>());
