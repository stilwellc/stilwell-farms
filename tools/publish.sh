#!/bin/sh
# Publish a clean snapshot of the current commit to the public GitHub repo, plus a ROM release.
# usage: tools/publish.sh v0.1 "Release title"
set -e
cd "$(dirname "$0")/.."
VER=${1:?usage: tools/publish.sh VERSION [TITLE]}
TITLE=${2:-"Stilwell Farms $VER"}
REPO=${REPO:-stilwellc/stilwell-farms}
PUB=${PUB:-$HOME/Dev/stilwell-farms-public}
if [ ! -d "$PUB/.git" ]; then
  mkdir -p "$PUB"
  git -C "$PUB" init -q -b main
  git -C "$PUB" remote add origin "https://github.com/$REPO.git"
  git -C "$PUB" fetch -q origin main 2>/dev/null && git -C "$PUB" reset -q --hard origin/main || true
fi
find "$PUB" -mindepth 1 -maxdepth 1 ! -name .git -exec rm -rf {} +
git archive HEAD | tar -x -C "$PUB"
sed -i '' '/^\.claude\//d' "$PUB/.gitignore"
git -C "$PUB" add -A
git -C "$PUB" commit -q -m "$TITLE" || echo "nothing new to commit"
git -C "$PUB" push -q -u origin main
./build.sh >/dev/null
gh release create "$VER" build/stilwell_farms.gbc --repo "$REPO" --title "$TITLE" \
  --notes "Download **stilwell_farms.gbc** below and play it on a ModRetro Chromatic, a flash cart, or any Game Boy Color emulator. See the README for install steps." \
  || gh release upload "$VER" build/stilwell_farms.gbc --repo "$REPO" --clobber
echo "published $VER -> https://github.com/$REPO"
