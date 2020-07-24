#!/usr/bin/env bash

source common.sh

clearStore
clearCache

export REMOTE_STORE=file://$cacheDir

drv=$(nix-instantiate ./content-addressed.nix --arg seed 1)
nix show-derivation --derivation "$drv" --arg seed 1

#out1=$(nix-build ./content-addressed.nix --arg seed 1)
#out2=$(nix-build ./content-addressed.nix --arg seed 2)
#
#test $out1 == $out2
