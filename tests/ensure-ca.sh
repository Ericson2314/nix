source common.sh

if [[ -z $(type -p jq) ]]; then
    echo "Jq not installed; skipping ensure-ca tests"
    exit 99
fi

# This are for ./fixed.nix
export IMPURE_VAR1=foo
export IMPURE_VAR2=bar

body=$(nix-build fixed.nix -A good.0 --no-out-link)

ca=$(nix path-info --json $body | jq -r .\[0\].ca)

path=$(nix ensure-ca $ca fixed)

[ $body = $path ]

# dependencies.nix has references, but we can’t calculate what they
# are without building it!

body=$(nix-build dependencies.nix --no-out-link)

rewrite=$(nix --experimental-features 'nix-command ca-references' make-content-addressable --json -r $body | jq -r ".rewrites[\"$body\"]")

ca=$(nix path-info --json $rewrite | jq -r .\[0\].ca)

(! nix ensure-ca $ca dependencies-top)
