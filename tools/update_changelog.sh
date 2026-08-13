#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

DESCRIBE="$(git describe --tags --long --always | sed 's/^v//')"
if [[ -z "$DESCRIBE" ]]; then
  VERSION="0.1.0-dev"
  RELEASE="1"
else
  # Extract base version and commit count
  BASE="${DESCRIBE%%-*}"
  # Remove leading v
  BASE="${BASE#v}"
  VERSION="$BASE"
  if [[ "$DESCRIBE" =~ -([0-9]+)-g ]]; then
    RELEASE="${BASH_REMATCH[1]}"
  else
    RELEASE="1"
  fi
fi

CHANGELOG="$REPO_ROOT/debian/changelog"
DATE="$(date -R)"

# If changelog exists, prepend new entry
if [[ -f "$CHANGELOG" ]]; then
  # Keep existing content
  EXISTING="$(cat "$CHANGELOG")"
  cat > "$CHANGELOG" <<EOF
whisper-echo (${VERSION}-${RELEASE}) unstable; urgency=medium

  * Automated changelog update for version ${VERSION}-${RELEASE}

 -- Bryan Nielsen <bnielsen1965@gmail.com>  ${DATE}

${EXISTING}
EOF
else
  cat > "$CHANGELOG" <<EOF
whisper-echo (${VERSION}-${RELEASE}) unstable; urgency=medium

  * Initial automated changelog

 -- Bryan Nielsen <bnielsen1965@gmail.com>  ${DATE}
EOF
fi

echo "Updated $CHANGELOG to ${VERSION}-${RELEASE}"
