source common.sh

sed -i 's/experimental-features .*/& ca-derivations/' "$NIX_CONF_DIR"/nix.conf

export NIX_TESTS_CA_BY_DEFAULT=1

cd ..
source ./build.sh
