#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

VERSION="$(git describe --tags --abbrev=0 --always | sed 's/^v//')"
if [[ -z "$VERSION" ]]; then
  VERSION="0.1.0-dev"
fi

echo "Ensuring submodules..."
git submodule update --init --recursive

OUT="whisper-echo-${VERSION}.tar.gz"
echo "Creating $OUT with submodules..."
# Tar the repo excluding .git and any existing tarballs
tar --exclude='.git' --exclude='*.tar.gz' -czf "$OUT" -C "$REPO_ROOT" .

echo "Done: $OUT"
