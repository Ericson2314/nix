#include "serialise.hh"
#include "util.hh"
#include "path-with-outputs.hh"
#include "store-api.hh"
#include "build-result.hh"
#include "worker-protocol.hh"
#include "worker-protocol-impl.hh"
#include "archive.hh"
#include "path-info.hh"

#include <nlohmann/json.hpp>

namespace nix {
namespace worker_proto {

/* protocol-agnostic definitions */
#include "gen-protocol.cc-inc"

/* protocol-specific definitions */

BuildResult read(const Store & store, unsigned int version, Source & from, Phantom<BuildResult> _)
{
    auto path = worker_proto::read(store, version, from, Phantom<DerivedPath> {});
    BuildResult res { .path = path };
    res.status = (BuildResult::Status) readInt(from);
    from
        >> res.errorMsg
        >> res.timesBuilt
        >> res.isNonDeterministic
        >> res.startTime
        >> res.stopTime;
    res.builtOutputs = worker_proto::read(store, version, from, Phantom<DrvOutputs> {});
    return res;
}

void write(const Store & store, unsigned int version, Sink & to, const BuildResult & res)
{
    worker_proto::write(store, version, to, res.path);
    to
        << res.status
        << res.errorMsg
        << res.timesBuilt
        << res.isNonDeterministic
        << res.startTime
        << res.stopTime;
    worker_proto::write(store, version, to, res.builtOutputs);
}


ValidPathInfo readValidPathInfo(const Store & store, unsigned int version, Source & source)
{
    auto path = read(store, version, source, Phantom<StorePath>{});
    return readValidPathInfo(store, version, source, std::move(path));
}

ValidPathInfo readValidPathInfo(const Store & store, unsigned int version, Source & source, StorePath && path)
{
    auto deriver = readString(source);
    auto narHash = Hash::parseAny(readString(source), htSHA256);
    ValidPathInfo info(path, narHash);
    if (deriver != "") info.deriver = store.parseStorePath(deriver);
    info.references = read(store, version, source, Phantom<StorePathSet> {});
    source >> info.registrationTime >> info.narSize;
    if (GET_PROTOCOL_MINOR(version) >= 16) {
        source >> info.ultimate;
        info.sigs = readStrings<StringSet>(source);
        info.ca = parseContentAddressOpt(readString(source));
    }
    return info;
}

void write(
    const Store & store,
    unsigned int version,
    Sink & sink,
    const ValidPathInfo & pathInfo,
    bool includePath)
{
    if (includePath)
        sink << store.printStorePath(pathInfo.path);
    sink << (pathInfo.deriver ? store.printStorePath(*pathInfo.deriver) : "")
         << pathInfo.narHash.to_string(Base16, false);
    write(store, version, sink, pathInfo.references);
    sink << pathInfo.registrationTime << pathInfo.narSize;
    if (GET_PROTOCOL_MINOR(version) >= 16) {
        sink << pathInfo.ultimate
             << pathInfo.sigs
             << renderContentAddress(pathInfo.ca);
    }
}

}
}
