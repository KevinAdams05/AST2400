#!/usr/bin/env bash
#
# Wrap the built AST2400 kernel driver into a .hpkg.
#
# Run scripts/build.sh first to produce build/<ARCH>/ast.
#
# Layout inside the .hpkg:
#   add-ons/kernel/drivers/bin/ast
#   add-ons/kernel/drivers/dev/graphics/ast  -> ../../bin/ast
#
# Install path: per-user, by dropping the .hpkg in ~/config/packages/.
#
# Phase 1 caveat: this package ships only the kernel driver. There is no
# accelerant yet, so app_server cannot drive a display through this
# driver — it will probe and bind in syslog, then app_server will fall
# back to VESA. Phase 2 will add the accelerant.
#
# Usage:
#   scripts/package.sh [VERSION]
#
# VERSION defaults to 0.0.$(date +%Y%m%d) if not given.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
ARCH="${ARCH:-x86_64}"
VERSION="${1:-${VERSION:-0.0.$(date +%Y%m%d)}}"

# `package create` is a Haiku host tool. On a Haiku build host it's on $PATH;
# on a Linux build host it lives in the cross-tools build output.
HAIKU_SRC="${HAIKU_SRC:-$HOME/haiku-build/haiku}"
HOST_PACKAGE_BIN="$HAIKU_SRC/generated.$ARCH/objects/linux/x86_64/release/tools/package/package"
if command -v package >/dev/null 2>&1; then
	PACKAGE_CMD="$(command -v package)"
elif [ -x "$HOST_PACKAGE_BIN" ]; then
	PACKAGE_CMD="$HOST_PACKAGE_BIN"
else
	echo "ERROR: Haiku 'package' tool not found." >&2
	echo "       Tried PATH and $HOST_PACKAGE_BIN" >&2
	exit 1
fi

BUILD_DIR="${REPO_ROOT}/build/${ARCH}"
DIST_DIR="${REPO_ROOT}/dist"
STAGING="${REPO_ROOT}/build/staging-${ARCH}"
KERNEL_DRV="${BUILD_DIR}/ast"

if [ ! -f "$KERNEL_DRV" ]; then
	echo "ERROR: built kernel driver missing: $KERNEL_DRV" >&2
	echo "       Run scripts/build.sh first." >&2
	exit 1
fi

echo "==> Staging package contents in $STAGING"
rm -rf "$STAGING"
mkdir -p "$STAGING/add-ons/kernel/drivers/bin"
mkdir -p "$STAGING/add-ons/kernel/drivers/dev/graphics"

cp -v "$KERNEL_DRV" "$STAGING/add-ons/kernel/drivers/bin/ast"
ln -sf "../../bin/ast" "$STAGING/add-ons/kernel/drivers/dev/graphics/ast"

echo "==> Generating .PackageInfo"
sed "s/@VERSION@/$VERSION/g" "$REPO_ROOT/packaging/PackageInfo.in" \
	> "$STAGING/.PackageInfo"

mkdir -p "$DIST_DIR"
OUTPUT_HPKG="$DIST_DIR/aspeed_gfx_unofficial-$VERSION-$ARCH.hpkg"

echo "==> Creating $OUTPUT_HPKG"
rm -f "$OUTPUT_HPKG"
( cd "$STAGING" && "$PACKAGE_CMD" create "$OUTPUT_HPKG" )

echo "==> Package contents:"
"$PACKAGE_CMD" list "$OUTPUT_HPKG"

echo ""
echo "==> Done: $OUTPUT_HPKG"
ls -la "$OUTPUT_HPKG"
