source $(dirname "${BASH_SOURCE[0]}")/../common.sh

sed -i 's/experimental-features .*/& ca-derivations ca-references/' "$NIX_CONF_DIR"/nix.conf

restartDaemon
