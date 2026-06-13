#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
REPO_ROOT="$(cd "$ROOT/.." && pwd)"
BUNDLE="${1:-$ROOT/delegate-injection/bin/libh265_delegate_bundle_probe.so}"
RELEASE_TAG="${H265_DELEGATE_BUNDLE_RELEASE:-h265-delegate-bundle}"
ASSET_NAME="${H265_DELEGATE_BUNDLE_ASSET:-libh265_delegate_bundle_probe.so}"
TITLE="HEVC delegate bundle"

if [[ ! -f "$BUNDLE" ]]; then
  echo "missing delegate bundle: $BUNDLE" >&2
  echo "build it first with: $ROOT/delegate-injection/build-self-contained-bundle.sh" >&2
  exit 1
fi

if ! command -v gh >/dev/null 2>&1; then
  echo "missing required command: gh" >&2
  exit 1
fi

REPO="${GITHUB_REPOSITORY:-}"
if [[ -z "$REPO" ]]; then
  REPO="$(gh repo view --json nameWithOwner --jq .nameWithOwner)"
fi

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

install -m 0755 "$BUNDLE" "$tmp/$ASSET_NAME"
sha256sum "$tmp/$ASSET_NAME" >"$tmp/$ASSET_NAME.sha256"

source_commit="$(git -C "$REPO_ROOT" rev-parse --short=12 HEAD 2>/dev/null || echo unknown)"
bundle_sha="$(cut -d' ' -f1 "$tmp/$ASSET_NAME.sha256")"
cat >"$tmp/notes.md" <<EOF
Maintenance asset used by the release workflow.

This is not a user-facing runtime package. The release workflow downloads this
asset and packages it into chrome-vaapi-h264-h265-hotpatch.tar.gz.

Source commit: $source_commit
SHA256: $bundle_sha

Rebuild from this repository with:

    cd h264-h265
    ./delegate-injection/build-self-contained-bundle.sh
EOF

if gh release view "$RELEASE_TAG" --repo "$REPO" >/dev/null 2>&1; then
  gh release upload "$RELEASE_TAG" \
    "$tmp/$ASSET_NAME" \
    "$tmp/$ASSET_NAME.sha256" \
    --repo "$REPO" \
    --clobber
  gh release edit "$RELEASE_TAG" \
    --repo "$REPO" \
    --title "$TITLE" \
    --notes-file "$tmp/notes.md" \
    --prerelease
else
  gh release create "$RELEASE_TAG" \
    "$tmp/$ASSET_NAME" \
    "$tmp/$ASSET_NAME.sha256" \
    --repo "$REPO" \
    --title "$TITLE" \
    --notes-file "$tmp/notes.md" \
    --prerelease
fi

echo "published $ASSET_NAME to $REPO release $RELEASE_TAG"
