#!/usr/bin/env bash
#
# Build the AST2400 kernel driver by overlaying this repo's source onto a
# Haiku source tree and running jam.
#
# Usage:
#   scripts/build.sh [HAIKU_SRC]
#
# Arguments / environment:
#   HAIKU_SRC  Path to a Haiku source tree with cross-tools already
#              configured (defaults to $HOME/haiku-build/haiku).
#   ARCH       Target arch (defaults to x86_64).
#
# Outputs:
#   build/<ARCH>/ast       kernel driver binary
#
# Notes:
#   The ast/ directory does not exist in upstream Haiku, so this script
#   creates it under the Haiku source tree and registers it with the
#   parent Jamfile via SubInclude. The change to the Haiku tree is
#   in-place; it is harmless if run repeatedly but leaves the Haiku
#   tree slightly modified vs upstream.

set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HAIKU_SRC="${1:-${HAIKU_SRC:-$HOME/haiku-build/haiku}}"
ARCH="${ARCH:-x86_64}"
OUTPUT_DIR="${REPO_ROOT}/build/${ARCH}"

if [ ! -d "$HAIKU_SRC" ]; then
	echo "ERROR: HAIKU_SRC='$HAIKU_SRC' does not exist." >&2
	echo "Pass a Haiku source tree path as the first argument, or set" >&2
	echo "the HAIKU_SRC environment variable." >&2
	exit 1
fi

GENERATED="$HAIKU_SRC/generated.$ARCH"
if [ ! -d "$GENERATED" ]; then
	echo "ERROR: '$GENERATED' does not exist." >&2
	echo "Configure the Haiku tree for $ARCH cross-tools first." >&2
	exit 1
fi

KERNEL_DRIVER_DST="$HAIKU_SRC/src/add-ons/kernel/drivers/graphics/ast"
ACCELERANT_DST="$HAIKU_SRC/src/add-ons/accelerants/ast"
HEADERS_DST="$HAIKU_SRC/headers/private/graphics/ast"

echo "==> Creating ast/ subdirs in Haiku tree (no-op if already present)"
mkdir -p "$KERNEL_DRIVER_DST" "$ACCELERANT_DST" "$HEADERS_DST"

echo "==> Overlaying source"
cp -v "$REPO_ROOT/src/add-ons/kernel/drivers/graphics/ast/"* \
	"$KERNEL_DRIVER_DST/"
cp -v "$REPO_ROOT/src/add-ons/accelerants/ast/"* \
	"$ACCELERANT_DST/"
cp -v "$REPO_ROOT/headers/private/graphics/ast/"*.h \
	"$HEADERS_DST/"

# Register the new subdirs with the parent Jamfiles if they aren't there yet.
DRIVER_PARENT_JAMFILE="$HAIKU_SRC/src/add-ons/kernel/drivers/graphics/Jamfile"
if ! grep -q "graphics ast ;" "$DRIVER_PARENT_JAMFILE" 2>/dev/null; then
	echo "==> Registering kernel driver ast/ with parent Jamfile"
	echo "SubInclude HAIKU_TOP src add-ons kernel drivers graphics ast ;" \
		>> "$DRIVER_PARENT_JAMFILE"
fi

ACCELERANT_PARENT_JAMFILE="$HAIKU_SRC/src/add-ons/accelerants/Jamfile"
if ! grep -q "accelerants ast ;" "$ACCELERANT_PARENT_JAMFILE" 2>/dev/null; then
	echo "==> Registering accelerant ast/ with parent Jamfile"
	echo "SubInclude HAIKU_TOP src add-ons accelerants ast ;" \
		>> "$ACCELERANT_PARENT_JAMFILE"
fi

echo ""
echo "==> Building"
cd "$GENERATED"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
jam -q -j"$JOBS" ast ast.accelerant

echo ""
echo "==> Extracting binaries to $OUTPUT_DIR"
mkdir -p "$OUTPUT_DIR"
BUILT_DRIVER="$GENERATED/objects/haiku/$ARCH/release/add-ons/kernel/drivers/graphics/ast/ast"
BUILT_ACCEL="$GENERATED/objects/haiku/$ARCH/release/add-ons/accelerants/ast/ast.accelerant"
if [ ! -f "$BUILT_DRIVER" ]; then
	echo "ERROR: expected kernel driver not found: $BUILT_DRIVER" >&2
	exit 1
fi
if [ ! -f "$BUILT_ACCEL" ]; then
	echo "ERROR: expected accelerant not found: $BUILT_ACCEL" >&2
	exit 1
fi
cp -v "$BUILT_DRIVER" "$OUTPUT_DIR/ast"
cp -v "$BUILT_ACCEL" "$OUTPUT_DIR/ast.accelerant"

echo ""
echo "==> Build complete:"
ls -la "$OUTPUT_DIR"
