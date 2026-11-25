#!/bin/bash
# Opens the VitaDB submission for the Vita port of Ghost in the Shell: First Assault: an issue on
# github.com/Rinnegatamante/VitaDB-contributions, posted from your GitHub account (run ../github/publish.sh first).
# Usage, in WSL, from this folder:   bash submit-vitadb.sh
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
LOGIN=$(gh api user --jq .login)
gh release view v0.9 --repo "$LOGIN/gitsfa-vita" >/dev/null 2>&1 \
  || { echo "github.com/$LOGIN/gitsfa-vita has no v0.9 release yet: run ../github/publish.sh first"; exit 1; }
BODY=$(sed "s#OWNER#$LOGIN#g" "$HERE/issue.md")
TITLE="[NEW] Ghost in the Shell: First Assault"
printf '%s\n\n%s\n\n' "$TITLE" "$BODY"
read -r -p "Post this as a new issue on Rinnegatamante/VitaDB-contributions as $LOGIN? [y/N] " ok
[[ "$ok" == [yY] ]] || { echo "stopped, nothing was posted"; exit 0; }
gh issue create --repo Rinnegatamante/VitaDB-contributions --title "$TITLE" --body "$BODY"
