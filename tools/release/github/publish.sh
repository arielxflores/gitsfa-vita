#!/bin/bash
# Puts the Vita port of Ghost in the Shell: First Assault on your GitHub account: creates the public repository
# gitsfa-vita from gitsfa-vita-source.tar.gz (the source WITHOUT any game data), and publishes release v1.0 with
# GITSFA-Vita.vpk, the data builder's zip and the screenshots.
# Usage, in WSL (Ubuntu), from this folder:   bash publish.sh
# It asks before creating anything, and stops if the repository already exists.
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
REPO=gitsfa-vita
TAG=v1.0
DEST="$HOME/$REPO"
VPK="$HERE/../GITSFA-Vita.vpk"
ZIP="$HERE/../GITSFA-Vita-DataBuilder-win64.zip"
SRC="$HERE/gitsfa-vita-source.tar.gz"
for f in "$VPK" "$ZIP" "$SRC" "$HERE/release-notes.md"; do [ -f "$f" ] || { echo "can't find $f"; exit 1; }; done
ls "$HERE"/screenshots/*.jpg >/dev/null 2>&1 || { echo "can't find the screenshots"; exit 1; }

# nothing of the game may go out: the source archive is checked once more here
bad=$(tar tzf "$SRC" | grep -iE '\.(g[a-z][a-z]|gab|dds|tga|nif|pkl|npy|sdf|ssf|wav|mp4|jpg|fsb|dat|suprx|vpk)$' || true)
[ -z "$bad" ] || { echo "the source archive holds files that must not be published:"; echo "$bad" | head; exit 1; }

ver_ok() { local v; v=$(gh --version 2>/dev/null | head -1 | grep -o '[0-9]*\.[0-9]*' | head -1) || return 1
  [ -n "$v" ] && [ "${v%%.*}" -ge 2 ] && { [ "${v%%.*}" -gt 2 ] || [ "${v#*.}" -ge 40 ]; }; }
if ! command -v git >/dev/null; then sudo apt update && sudo apt install -y git; fi
if ! ver_ok; then
  echo "== installing the GitHub CLI (gh) from cli.github.com"
  sudo mkdir -p -m 755 /etc/apt/keyrings
  wget -qO- https://cli.github.com/packages/githubcli-archive-keyring.gpg | sudo tee /etc/apt/keyrings/githubcli-archive-keyring.gpg >/dev/null
  sudo chmod go+r /etc/apt/keyrings/githubcli-archive-keyring.gpg
  echo "deb [arch=$(dpkg --print-architecture) signed-by=/etc/apt/keyrings/githubcli-archive-keyring.gpg] https://cli.github.com/packages stable main" \
    | sudo tee /etc/apt/sources.list.d/github-cli.list >/dev/null
  sudo apt update && sudo apt install -y gh
fi
if ! gh auth status >/dev/null 2>&1; then
  echo "== sign in to GitHub: copy the code it shows, open the link in your browser and paste it there"
  gh auth login --hostname github.com --git-protocol https --web
fi
gh auth setup-git
LOGIN=$(gh api user --jq .login); ID=$(gh api user --jq .id)
echo "== GitHub account: $LOGIN"
if gh repo view "$LOGIN/$REPO" >/dev/null 2>&1; then echo "github.com/$LOGIN/$REPO already exists, stopping (nothing was changed)"; exit 1; fi
if [ -e "$DEST" ]; then echo "$DEST already exists (from an earlier try?): move it away, then run this again"; exit 1; fi

echo
echo "This will:"
echo "  - create the PUBLIC repository github.com/$LOGIN/$REPO with the port's source ($(tar tzf "$SRC" | grep -vc '/$') files, no game data)"
echo "    as one commit by $LOGIN (GitHub's no-reply address: your e-mail address is not put into it)"
echo "  - publish release $TAG with:"
echo "      GITSFA-Vita.vpk                      $(du -h "$VPK" | cut -f1)"
echo "      GITSFA-Vita-DataBuilder-win64.zip    $(du -h "$ZIP" | cut -f1)"
echo "      $(ls "$HERE"/screenshots/*.jpg | wc -l) screenshots"
read -r -p "Go ahead? [y/N] " ok
[[ "$ok" == [yY] ]] || { echo "stopped, nothing was published"; exit 0; }

mkdir -p "$DEST"; tar xzf "$SRC" -C "$DEST" --strip-components=1
mkdir -p "$DEST/docs/screenshots"; cp "$HERE"/screenshots/*.jpg "$DEST/docs/screenshots/"   # (the README shows them; your own pictures from the Vita)
cd "$DEST"
git init -q -b main
git config user.name "$LOGIN"; git config user.email "$ID+$LOGIN@users.noreply.github.com"
git add -A
git commit -q -m "Ghost in the Shell: First Assault for PS Vita, 1.0 (build 123)

The port's source: the engine (engine/), the data builder and its converters (tools/, vm/), the development log (docs/).
No game data.

Co-Authored-By: Claude Fable 5.2 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_016bkSsSuU3stc4E7jeciNFC"
echo "== creating github.com/$LOGIN/$REPO and pushing"
gh repo create "$LOGIN/$REPO" --public --source . --remote origin --push \
  --description "Unofficial PS Vita version of Ghost in the Shell: Stand Alone Complex - First Assault Online (offline, bots, multiplayer). Game data not included."
gh repo edit "$LOGIN/$REPO" --add-topic ps-vita --add-topic psvita --add-topic vitasdk --add-topic vitagl --add-topic homebrew \
  --add-topic ghost-in-the-shell >/dev/null || echo "(couldn't set the topics; not important)"
echo "== publishing release $TAG"
gh release create "$TAG" "$VPK" "$ZIP" "$HERE"/screenshots/*.jpg --repo "$LOGIN/$REPO" --target main \
  --title "1.0" --notes-file "$HERE/release-notes.md"
echo
echo "== done: https://github.com/$LOGIN/$REPO"
echo "Next, for VitaDB:  cd ../vitadb && bash submit-vitadb.sh"
