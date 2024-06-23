#include "gtest/gtest.h"
#include <string>
#include "tests/characterization.hh"
#include "tests/libexpr.hh"
#include "derivations.hh"

namespace nix {

class DerivationTest : public CharacterizationTest, public LibExprTest
{
    Path unitTestData = getUnitTestData() + "/derivation";

public:
    Path goldenMaster(std::string_view testStem) const override
    {
        return unitTestData + "/" + testStem;
    }
};

#define EXPR_DRV_TEST(NAME, STEM)                                                               \
    TEST_F(DerivationTest, Derivation_##NAME)                                                   \
    {                                                                                           \
        writeTest(                                                                              \
            STEM ".drv",                                                                        \
            [&]() -> Derivation {                                                               \
                Value v = eval(readFile(goldenMaster(STEM ".nix")));                            \
                Symbol s = state.symbols.create("drvPath");                                     \
                auto attr = v.attrs() -> get(s);                                                \
                state.forceValueDeep(*attr->value);                                             \
                NixStringContext context;                                                       \
                auto storePath = state.coerceToStorePath(attr->pos, *attr->value, context, ""); \
                                                                                                \
                return store->readDerivation(storePath);                                        \
            },                                                                                  \
            [&](const auto & file) {                                                            \
                auto s = readFile(file);                                                        \
                return parseDerivation(*store, std::move(s), STEM);                             \
            },                                                                                  \
            [&](const auto & file, const auto & got) {                                          \
                auto s = got.unparse(*store, false);                                            \
                return writeFile(file, std::move(s));                                           \
            });                                                                                 \
    }

EXPR_DRV_TEST(advancedAttributes, "advanced-attributes");
EXPR_DRV_TEST(advancedAttributes_defaults, "advanced-attributes-defaults");
EXPR_DRV_TEST(advancedAttributes_structuredAttrs, "advanced-attributes-structured-attrs");
EXPR_DRV_TEST(advancedAttributes_structuredAttrs_defaults, "advanced-attributes-structured-attrs-defaults");

}
