#include "derivations.hh"
#include "store-api.hh"
#include "globals.hh"
#include "util.hh"
#include "worker-protocol.hh"
#include "fs-accessor.hh"
#include "istringstream_nocopy.hh"

namespace nix {

// Avoid shadow
HashType parseHashAlgo(const string & s) {
    return parseHashType(s);
}

void DerivationOutput::parseHashType(FileIngestionMethod & recursive, HashType & hashType) const
{
    recursive = FileIngestionMethod::Flat;
    string algo = hashAlgo;

    if (string(algo, 0, 2) == "r:") {
        recursive = FileIngestionMethod::Recursive;
        algo = string(algo, 2);
    }

    HashType hashType_loc = parseHashAlgo(algo);
    if (hashType_loc == htUnknown)
        throw Error("unknown hash algorithm '%s'", algo);
    hashType = hashType_loc;
}

void DerivationOutput::parseHashInfo(FileIngestionMethod & recursive, Hash & hash) const
{
    HashType hashType;
    parseHashType(recursive, hashType);
    hash = Hash(this->hash, hashType);
}


bool derivationIsCA(DerivationType dt) {
    switch (dt) {
    case DerivationType::Regular: return false;
    case DerivationType::CAFixed: return true;
    };
    // Since enums can have non-variant values, but making a `default:` would
    // disable exhaustiveness warnings.
    abort();
}

bool derivationIsFixed(DerivationType dt) {
    switch (dt) {
    case DerivationType::Regular: return false;
    case DerivationType::CAFixed: return true;
    };
    abort();
}

bool derivationIsImpure(DerivationType dt) {
    switch (dt) {
    case DerivationType::Regular: return false;
    case DerivationType::CAFixed: return true;
    };
    abort();
}

BasicDerivation::BasicDerivation(const BasicDerivation & other)
    : platform(other.platform)
    , builder(other.builder)
    , args(other.args)
    , env(other.env)
{
    for (auto & i : other.outputs)
        outputs.insert_or_assign(i.first,
            DerivationOutput(
                i.second.path
                  ? std::optional<StorePath>((*i.second.path).clone())
                  : std::optional<StorePath>(),
                std::string(i.second.hashAlgo),
                std::string(i.second.hash)));
    for (auto & i : other.inputSrcs)
        inputSrcs.insert(i.clone());
}


Derivation::Derivation(const Derivation & other)
    : BasicDerivation(other)
{
    for (auto & i : other.inputDrvs)
        inputDrvs.insert_or_assign(i.first.clone(), i.second);
}


const std::optional<StorePath> & BasicDerivation::findOutput(const string & id) const
{
    auto i = outputs.find(id);
    if (i == outputs.end())
        throw Error("derivation has no output '%s'", id);
    return i->second.path;
}


bool BasicDerivation::isBuiltin() const
{
    return string(builder, 0, 8) == "builtin:";
}


StorePath writeDerivation(ref<Store> store,
    const Derivation & drv, std::string_view name, RepairFlag repair)
{
    auto references = cloneStorePathSet(drv.inputSrcs);
    for (auto & i : drv.inputDrvs)
        references.insert(i.first.clone());
    /* Note that the outputs of a derivation are *not* references
       (that can be missing (of course) and should not necessarily be
       held during a garbage collection). */
    auto suffix = std::string(name) + drvExtension;
    auto contents = drv.unparse(*store, false);
    return settings.readOnlyMode
        ? store->computeStorePathForText(suffix, contents, references)
        : store->addTextToStore(suffix, contents, references, repair);
}


/* Read string `s' from stream `str'. */
static void expect(std::istream & str, const string & s)
{
    char s2[s.size()];
    str.read(s2, s.size());
    if (string(s2, s.size()) != s)
        throw FormatError(format("expected string '%1%'") % s);
}


/* Read a C-style string from stream `str'. */
static string parseString(std::istream & str)
{
    string res;
    expect(str, "\"");
    int c;
    while ((c = str.get()) != '"')
        if (c == '\\') {
            c = str.get();
            if (c == 'n') res += '\n';
            else if (c == 'r') res += '\r';
            else if (c == 't') res += '\t';
            else res += c;
        }
        else res += c;
    return res;
}


static Path parsePath(std::istream & str)
{
    string s = parseString(str);
    if (s.size() == 0 || s[0] != '/')
        throw FormatError(format("bad path '%1%' in derivation") % s);
    return s;
}


static bool endOfList(std::istream & str)
{
    if (str.peek() == ',') {
        str.get();
        return false;
    }
    if (str.peek() == ']') {
        str.get();
        return true;
    }
    return false;
}


static StringSet parseStrings(std::istream & str, bool arePaths)
{
    StringSet res;
    while (!endOfList(str))
        res.insert(arePaths ? parsePath(str) : parseString(str));
    return res;
}


static Derivation parseDerivation(const Store & store, const string & s)
{
    Derivation drv;
    istringstream_nocopy str(s);
    expect(str, "Derive([");

    /* Parse the list of outputs. */
    while (!endOfList(str)) {
        expect(str, "("); std::string id = parseString(str);
        expect(str, ","); auto path = store.parseStorePath(parsePath(str));
        expect(str, ","); auto hashAlgo = parseString(str);
        expect(str, ","); auto hash = parseString(str);
        expect(str, ")");
        drv.outputs.emplace(id, DerivationOutput(std::move(path), std::move(hashAlgo), std::move(hash)));
    }

    /* Parse the list of input derivations. */
    expect(str, ",[");
    while (!endOfList(str)) {
        expect(str, "(");
        Path drvPath = parsePath(str);
        expect(str, ",[");
        drv.inputDrvs.insert_or_assign(store.parseStorePath(drvPath), parseStrings(str, false));
        expect(str, ")");
    }

    expect(str, ",["); drv.inputSrcs = store.parseStorePathSet(parseStrings(str, true));
    expect(str, ","); drv.platform = parseString(str);
    expect(str, ","); drv.builder = parseString(str);

    /* Parse the builder arguments. */
    expect(str, ",[");
    while (!endOfList(str))
        drv.args.push_back(parseString(str));

    /* Parse the environment variables. */
    expect(str, ",[");
    while (!endOfList(str)) {
        expect(str, "("); string name = parseString(str);
        expect(str, ","); string value = parseString(str);
        expect(str, ")");
        drv.env[name] = value;
    }

    expect(str, ")");
    return drv;
}


Derivation readDerivation(const Store & store, const Path & drvPath)
{
    try {
        return parseDerivation(store, readFile(drvPath));
    } catch (FormatError & e) {
        throw Error(format("error parsing derivation '%1%': %2%") % drvPath % e.msg());
    }
}


Derivation Store::derivationFromPath(const StorePath & drvPath)
{
    ensurePath(drvPath);
    auto accessor = getFSAccessor();
    try {
        return parseDerivation(*this, accessor->readFile(printStorePath(drvPath)));
    } catch (FormatError & e) {
        throw Error("error parsing derivation '%s': %s", printStorePath(drvPath), e.msg());
    }
}


static void printString(string & res, std::string_view s)
{
    char buf[s.size() * 2 + 2];
    char * p = buf;
    *p++ = '"';
    for (auto c : s)
        if (c == '\"' || c == '\\') { *p++ = '\\'; *p++ = c; }
        else if (c == '\n') { *p++ = '\\'; *p++ = 'n'; }
        else if (c == '\r') { *p++ = '\\'; *p++ = 'r'; }
        else if (c == '\t') { *p++ = '\\'; *p++ = 't'; }
        else *p++ = c;
    *p++ = '"';
    res.append(buf, p - buf);
}


static void printUnquotedString(string & res, std::string_view s)
{
    res += '"';
    res.append(s);
    res += '"';
}


template<class ForwardIterator>
static void printStrings(string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printString(res, *i);
    }
    res += ']';
}


template<class ForwardIterator>
static void printUnquotedStrings(string & res, ForwardIterator i, ForwardIterator j)
{
    res += '[';
    bool first = true;
    for ( ; i != j; ++i) {
        if (first) first = false; else res += ',';
        printUnquotedString(res, *i);
    }
    res += ']';
}


string Derivation::unparse(const Store & store, bool maskOutputs,
    std::map<std::string, StringSet> * actualInputs) const
{
    string s;
    s.reserve(65536);
    s += "Derive([";

    bool first = true;
    for (auto & i : outputs) {
        if (first) first = false; else s += ',';
        s += '('; printUnquotedString(s, i.first);
        s += ','; printUnquotedString(s, i.second.path || maskOutputs ? "" : store.printStorePath(*i.second.path));
        s += ','; printUnquotedString(s, i.second.hashAlgo);
        s += ','; printUnquotedString(s, i.second.hash);
        s += ')';
    }

    s += "],[";
    first = true;
    if (actualInputs) {
        for (auto & i : *actualInputs) {
            if (first) first = false; else s += ',';
            s += '('; printUnquotedString(s, i.first);
            s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
            s += ')';
        }
    } else {
        for (auto & i : inputDrvs) {
            if (first) first = false; else s += ',';
            s += '('; printUnquotedString(s, store.printStorePath(i.first));
            s += ','; printUnquotedStrings(s, i.second.begin(), i.second.end());
            s += ')';
        }
    }

    s += "],";
    auto paths = store.printStorePathSet(inputSrcs); // FIXME: slow
    printUnquotedStrings(s, paths.begin(), paths.end());

    s += ','; printUnquotedString(s, platform);
    s += ','; printString(s, builder);
    s += ','; printStrings(s, args.begin(), args.end());

    s += ",[";
    first = true;
    for (auto & i : env) {
        if (first) first = false; else s += ',';
        s += '('; printString(s, i.first);
        s += ','; printString(s, maskOutputs && outputs.count(i.first) ? "" : i.second);
        s += ')';
    }

    s += "])";

    return s;
}


// FIXME: remove
bool isDerivation(const string & fileName)
{
    return hasSuffix(fileName, drvExtension);
}


DerivationType BasicDerivation::type() const
{
    if (outputs.size() == 1 &&
        outputs.begin()->first == "out" &&
        outputs.begin()->second.hash != "" &&
        !outputs.begin()->second.path)
    {
        return DerivationType::CAFixed;
    }

    auto const algo = outputs.begin()->second.hashAlgo;
    auto const type = algo == "" ? DerivationType::Regular : DerivationType::CAFloating;
    for (auto & i : outputs) {
        if (i.second.hash != "") {
            throw Error("Non-fixed-output derivation has fixed output");
        }
        if (i.second.hashAlgo != algo) {
            throw Error("Invalid mix of CA and regular outputs");
        }
        if ((algo == "") == (bool)i.second.path) {
            throw Error("Path must be blank if and only if floating CA drv");
        }
    }
    return type;
}


DrvHashes drvHashes;

/* pathDerivationModulo and hashDerivationModulo are mutually recursive
 */

/* Look up the derivation by value and memoize the
   `hashDerivationModulo` call.
 */
static DrvHashModulo & pathDerivationModulo(Store & store, const StorePath & drvPath)
{
    auto h = drvHashes.find(drvPath);
    if (h == drvHashes.end()) {
        assert(store.isValidPath(drvPath));
        // Cache it
        h = drvHashes.insert_or_assign(
            drvPath.clone(),
            hashDerivationModulo(
                store,
                readDerivation(
                    store,
                    store.toRealPath(store.printStorePath(drvPath))),
                false)).first;
    }
    return h->second;
}

/* See the header for interface details. These are the implementation details.

   For fixed-output derivations, each hash in the map is not the
   corresponding output's content hash, but a hash of that hash along
   with other constant data. The key point is that the value is a pure
   function of the output's contents, and there are no preimage attacks
   either spoofing an output's contents for a derivation, or
   spoofing a derivation for an output's contents.

   For regular derivations, it looks up each subderivation from its hash
   and recurs. If the subderivation is also regular, it simply
   substitutes the derivation path with its hash. If the subderivation
   is fixed-output, however, it takes each output hash and pretends it
   is a derivation hash producing a single "out" output. This is so we
   don't leak the provenance of fixed outputs, reducing pointless cache
   misses as the build itself won't know this.
 */
DrvHashModulo hashDerivationModulo(Store & store, const Derivation & drv, bool maskOutputs)
{
    /* Return a fixed hash for fixed-output derivations. */
    switch (drv.type()) {
    case DerivationType::CAFixed: {
        std::map<std::string, Hash> outputHashes;
        for (const auto & i : drv.outputs) {
            const Hash h = hashString(htSHA256, "fixed:out:"
                + i.second.hashAlgo + ":"
                + i.second.hash + ":"
                + store.printStorePath(i.second.path));
            outputHashes.insert_or_assign(std::string(i.first), std::move(h));
        }
        return outputHashes;
    }
    case DerivationType::CAFloating:
        throw Error("Floating CA derivations are unimplemented");
    default:
        break;
    }

    /* For other derivations, replace the inputs paths with recursive
       calls to this function. */
    std::map<std::string, StringSet> inputs2;
    for (auto & i : drv.inputDrvs) {
        const auto res = pathDerivationModulo(store, i.first);
        if (const Hash *pval = std::get_if<0>(&res)) {
            // regular non-CA derivation, replace derivation
            inputs2.insert_or_assign(pval->to_string(Base16, false), i.second);
        } else if (const std::map<std::string, Hash> *pval = std::get_if<1>(&res)) {
            // CA derivation's output hashes
            std::set justOut = { std::string("out") };
            for (auto & output : i.second) {
                /* Put each one in with a single "out" output.. */
                const auto h = pval->at(output);
                inputs2.insert_or_assign(
                    h.to_string(Base16, false),
                    justOut);
            }
        }
    }

    return hashString(htSHA256, drv.unparse(store, maskOutputs, &inputs2));
}


std::string StorePathWithOutputs::to_string(const Store & store) const
{
    return outputs.empty()
        ? store.printStorePath(path)
        : store.printStorePath(path) + "!" + concatStringsSep(",", outputs);
}


bool wantOutput(const string & output, const std::set<string> & wanted)
{
    return wanted.empty() || wanted.find(output) != wanted.end();
}


Source & readDerivation(Source & in, const Store & store, BasicDerivation & drv)
{
    drv.outputs.clear();
    auto nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto name = readString(in);
        auto path = store.parseStorePath(readString(in));
        auto hashAlgo = readString(in);
        auto hash = readString(in);
        drv.outputs.emplace(name, DerivationOutput(std::move(path), std::move(hashAlgo), std::move(hash)));
    }

    drv.inputSrcs = readStorePaths<StorePathSet>(store, in);
    in >> drv.platform >> drv.builder;
    drv.args = readStrings<Strings>(in);

    nr = readNum<size_t>(in);
    for (size_t n = 0; n < nr; n++) {
        auto key = readString(in);
        auto value = readString(in);
        drv.env[key] = value;
    }

    return in;
}


void writeDerivation(Sink & out, const Store & store, const BasicDerivation & drv)
{
    out << drv.outputs.size();
    for (auto & i : drv.outputs) {
        out << i.first;
        if (i.second.path) {
            out << store.printStorePath(*i.second.path);
        } else {
            out << "";
        }
        out << i.second.hashAlgo << i.second.hash;
    }
    writeStorePaths(store, out, drv.inputSrcs);
    out << drv.platform << drv.builder << drv.args;
    out << drv.env.size();
    for (auto & i : drv.env)
        out << i.first << i.second;
}


std::string hashPlaceholder(const std::string & outputName)
{
    // FIXME: memoize?
    return "/" + hashString(htSHA256, "nix-output:" + outputName).to_string(Base32, false);
}


}
