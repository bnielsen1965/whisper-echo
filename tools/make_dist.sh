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

# Create tarball with top-level directory whisper-echo-${VERSION}/
# Write outside repo first to avoid "file changed as we read it"
TMP_OUT="$REPO_ROOT/../$OUT"
tar --exclude='.git' --exclude='*.tar.gz' --exclude='build' --exclude='build/*' \
  --transform "s,^,whisper-echo-${VERSION}/," \
  -czf "$TMP_OUT" -C "$REPO_ROOT" .
mv "$TMP_OUT" "$REPO_ROOT/$OUT"

# Copy to rpmbuild SOURCES if rpmbuild is configured
if [ -d "$HOME/rpmbuild/SOURCES" ]; then
  cp "$REPO_ROOT/$OUT" "$HOME/rpmbuild/SOURCES/"
  echo "Copied $OUT to ~/rpmbuild/SOURCES/"
fi

echo "Done: $OUT"
