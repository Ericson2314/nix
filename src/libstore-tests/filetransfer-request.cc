#include <gtest/gtest.h>
#include <nlohmann/json.hpp>

#include "nix/store/filetransfer.hh"

namespace nix {

TEST(FileTransferRequest, displayUriStripsUserinfo)
{
    FileTransferRequest req(VerbatimURL{std::string{"https://alice:s3cr3t@example.org:8443/path/file.toml?x=1"}});
    // uri itself is untouched (used for CURLOPT_URL, result.urls, cache keys).
    EXPECT_EQ(req.uri.to_string(), "https://alice:s3cr3t@example.org:8443/path/file.toml?x=1");
    // displayUri() drops the userinfo for diagnostics.
    EXPECT_EQ(req.displayUri(), "https://example.org:8443/path/file.toml?x=1");

    FileTransferRequest plain(VerbatimURL{std::string{"https://example.org/file"}});
    EXPECT_EQ(plain.displayUri(), "https://example.org/file");
}

TEST(FileTransferError, toJSON)
{
    FileTransferError e(FileTransfer::NotFound, std::nullopt, "unable to download '%s'", "http://example.org");
    EXPECT_EQ(e.toJSON(), (nlohmann::json{{"type", "file-transfer"}, {"kind", "not-found"}}));

    FileTransferError withBody(
        FileTransfer::Misc, "<html>gone</html>", "unable to download '%s'", "http://example.org");
    EXPECT_EQ(
        withBody.toJSON(),
        (nlohmann::json{{"type", "file-transfer"}, {"kind", "misc"}, {"response", "<html>gone</html>"}}));
}

} // namespace nix
