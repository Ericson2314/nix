#include <gtest/gtest.h>
#include "nix/util/tests/json-characterization.hh"
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

class FileTransferErrorJsonTest : public virtual CharacterizationTest
{
    std::filesystem::path goldenMaster(std::string_view testStem) const override
    {
        return getUnitTestData() / "structured-error" / testStem;
    }
};

TEST_F(FileTransferErrorJsonTest, file_transfer)
{
    FileTransferError e(FileTransfer::NotFound, std::nullopt, "unable to download '%s'", "http://example.org");
    writeJsonTest(*this, "file_transfer", *e.toJSON());
}

TEST_F(FileTransferErrorJsonTest, file_transfer_with_response)
{
    FileTransferError e(FileTransfer::Misc, "<html>gone</html>", "unable to download '%s'", "http://example.org");
    writeJsonTest(*this, "file_transfer_with_response", *e.toJSON());
}

} // namespace nix
