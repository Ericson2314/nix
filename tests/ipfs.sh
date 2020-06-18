source common.sh

# This are for ./fixed.nix
export IMPURE_VAR1=foo
export IMPURE_VAR2=bar

################################################################################
## Check that the ipfs daemon is present and  enabled in your environment
################################################################################

if [[ -z $(type -p ipfs) ]]; then
    echo "Ipfs not installed; skipping ipfs tests"
    exit 99
fi

# To see if ipfs is connected to the network, we check if we can see some peers
# other than ourselves.
if (! (ipfs log ls));
then
  echo "Ipfs daemon not detected; initializing.."
  ipfs init
  ipfs daemon --offline &
  pidIpfsDaemon=$!
  trap "kill -9 $pidIpfsDaemon" EXIT
fi

clearStore

################################################################################
## Create the folders for the source and destination stores
################################################################################

IPFS_TESTS=$TEST_ROOT/ipfs_tests
mkdir $IPFS_TESTS

# Here we define some store locations, one for the initial store we upload, and
# the other three for the destination stores to which we'll copy (one for each
# method)
IPFS_SRC_STORE=$IPFS_TESTS/ipfs_source_store

IPFS_DST_HTTP_STORE=$IPFS_TESTS/ipfs_dest_http_store
IPFS_DST_HTTP_LOCAL_STORE=$IPFS_TESTS/ipfs_dest_http_local_store
IPFS_DST_IPFS_STORE=$IPFS_TESTS/ipfs_dest_ipfs_store
IPFS_DST_IPNS_STORE=$IPFS_TESTS/ipfs_dest_ipns_store

EMPTY_DIR_HASH=QmUNLLsPACCz1vLxQVkXqqLX5R1X345qqfHbsf67hvA3Nn

################################################################################
## Generate the keys to sign the store
################################################################################

SIGNING_KEY_NAME=nixcache.for.ipfs-1
SIGNING_KEY_PRI_FILE=$IPFS_TESTS/nix-cache-key.sec
SIGNING_KEY_PUB_FILE=$IPFS_TESTS/nix-cache-key.pub

nix-store --generate-binary-cache-key $SIGNING_KEY_NAME $SIGNING_KEY_PRI_FILE $SIGNING_KEY_PUB_FILE

################################################################################
## Create and sign the source store
################################################################################

mkdir -p $IPFS_SRC_STORE
storePaths=$(nix-build ./fixed.nix -A good)

nix sign-paths -k $SIGNING_KEY_PRI_FILE $storePaths

################################################################################
## Manually upload the source store
################################################################################

# Hack around https://github.com/NixOS/nix/issues/3695
for path in $storePaths; do
  nix copy --to file://$IPFS_SRC_STORE $path
done
unset path

MANUAL_IPFS_HASH=$(ipfs add -r $IPFS_SRC_STORE 2>/dev/null | tail -n 1 | awk '{print $2}')

################################################################################
## Create the local http store and download the derivation there
################################################################################

mkdir $IPFS_DST_HTTP_LOCAL_STORE

IPFS_HTTP_LOCAL_PREFIX='http://localhost:8080/ipfs'

DOWNLOAD_LOCATION=$(nix-build ./fixed.nix -A good \
  --option substituters $IPFS_HTTP_LOCAL_PREFIX/$MANUAL_IPFS_HASH \
  --store $IPFS_DST_HTTP_LOCAL_STORE \
  --no-out-link \
  -j0 \
  --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE))

################################################################################
## Create the ipfs store and download the derivation there
################################################################################

# Try to upload the content to the empty directory, fail but grab the right hash
IPFS_HASH=$(set -e; \
  set -o pipefail; \
  ! nix copy --to ipfs://$EMPTY_DIR_HASH $(nix-build ./fixed.nix -A good) --experimental-features nix-command \
    |& grep current: | awk '{print substr($2, 7, length($2))}')

# Upload the content with the right hash
nix copy --to ipfs://$IPFS_HASH $(nix-build ./fixed.nix -A good) --experimental-features nix-command

mkdir $IPFS_DST_IPFS_STORE

DOWNLOAD_LOCATION=$(nix-build ./fixed.nix -A good \
  --option substituters 'ipfs://'$IPFS_HASH \
  --store $IPFS_DST_IPFS_STORE \
  --no-out-link \
  -j0 \
  --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE))


################################################################################
## Create the ipns store and download the derivation there
################################################################################

# First I have to publish:
IPNS_ID=$(ipfs name publish $EMPTY_DIR_HASH --allow-offline | awk '{print substr($3,1,length($3)-1)}')

# Check that we can upload the ipns store directly
nix copy --to ipns://$IPNS_ID $(nix-build ./fixed.nix -A good) --experimental-features nix-command

mkdir $IPFS_DST_IPNS_STORE

DOWNLOAD_LOCATION=$(nix-build ./fixed.nix -A good \
  --option substituters 'ipns://'$IPNS_ID \
  --store $IPFS_DST_IPNS_STORE \
  --no-out-link \
  -j0 \
  --option trusted-public-keys $(cat $SIGNING_KEY_PUB_FILE))
