#include "archive.hh"
#include "fs-accessor.hh"
#include "store-api.hh"
#include "globals.hh"

namespace nix {

LocalFSStore::LocalFSStore(const Params & params)
    : Store(params)
    , rootDir(get(params, "root"))
    , stateDir(canonPath(get(params, "state", rootDir != "" ? rootDir + "/nix/var/nix" : settings.nixStateDir)))
    , logDir(canonPath(get(params, "log", rootDir != "" ? rootDir + "/nix/var/log/nix" : settings.nixLogDir)))
{
}

struct LocalStoreAccessor : public FSAccessor
{
    ref<LocalFSStore> store;

    LocalStoreAccessor(ref<LocalFSStore> store) : store(store) { }

    Path toRealPath(const Path & path)
    {
        Path storePath = store->toStorePath(path);
        if (!store->isValidPath(storePath))
            throw Error(format("path ‘%1%’ is not a valid store path") % storePath);
        return store->getRealStoreDir() + std::string(path, store->storeDir.size());
    }

    FSAccessor::Stat stat(const Path & path) override
    {
        auto realPath = toRealPath(path);

        struct stat st;
        if (lstat(path.c_str(), &st)) {
            if (errno == ENOENT || errno == ENOTDIR) return {Type::tMissing, 0, false};
            throw SysError(format("getting status of ‘%1%’") % path);
        }

        if (!S_ISREG(st.st_mode) && !S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode))
            throw Error(format("file ‘%1%’ has unsupported type") % path);

        return {
            S_ISREG(st.st_mode) ? Type::tRegular :
            S_ISLNK(st.st_mode) ? Type::tSymlink :
            Type::tDirectory,
            S_ISREG(st.st_mode) ? (uint64_t) st.st_size : 0,
            S_ISREG(st.st_mode) && st.st_mode & S_IXUSR};
    }

    StringSet readDirectory(const Path & path) override
    {
        auto realPath = toRealPath(path);

        auto entries = nix::readDirectory(path);

        StringSet res;
        for (auto & entry : entries)
            res.insert(entry.name);

        return res;
    }

    std::string readFile(const Path & path) override
    {
        return nix::readFile(toRealPath(path));
    }

    std::string readLink(const Path & path) override
    {
        return nix::readLink(toRealPath(path));
    }
};

ref<FSAccessor> LocalFSStore::getFSAccessor()
{
    return make_ref<LocalStoreAccessor>(ref<LocalFSStore>(std::dynamic_pointer_cast<LocalFSStore>(shared_from_this())));
}

void LocalFSStore::narFromPath(const Path & path, Sink & sink)
{
    if (!isValidPath(path))
        throw Error(format("path ‘%s’ is not valid") % path);
    dumpPath(path, sink);
}

}
