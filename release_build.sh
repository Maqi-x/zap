#!/usr/bin/env bash
set -euo pipefail

VERSION="0.4.0"
EXTENSION_VSIX="zap-${VERSION}.vsix"

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
BUILD_DIR="$SCRIPT_DIR/build"
ARCH=$(uname -m)
STAGE_DIR="$SCRIPT_DIR/zap-v${VERSION}-linux-${ARCH}"
TAR_FILE="$SCRIPT_DIR/zap-v${VERSION}-linux-${ARCH}.tar.gz"

CPU_COUNT=$(nproc 2>/dev/null || echo 1)
BUILD_JOBS=$((CPU_COUNT > 1 ? CPU_COUNT - 1 : 1))

echo "Configuring and building Zap compiler & LSP..."
cmake -S "$SCRIPT_DIR" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O2" \
    -DCMAKE_EXE_LINKER_FLAGS="-static-libstdc++ -static-libgcc" \
    -DLLVM_LINK_LLVM_DYLIB=OFF
cmake --build "$BUILD_DIR" --parallel "$BUILD_JOBS"

echo "Compiling stdlib.o..."
cc -c "$SCRIPT_DIR/src/stdlib.c" -o "$BUILD_DIR/stdlib.o"

echo "Packaging VS Code extension..."
EXTENSION_SOURCE_DIR="$SCRIPT_DIR/src/lsp/vscode/zap"
EXTENSION_BUILD_DIR="$(mktemp -d "${TMPDIR:-/tmp}/zap-vscode.XXXXXX")"
cleanup() {
  rm -rf "$EXTENSION_BUILD_DIR"
}
trap cleanup EXIT INT TERM

tar -C "$EXTENSION_SOURCE_DIR" \
    --exclude='./node_modules' \
    --exclude='./bin' \
    --exclude='./out' \
    --exclude='./package-lock.json' \
    --exclude='./zap-*.vsix' \
    -cf - . | tar -C "$EXTENSION_BUILD_DIR" -xf -

(
  cd "$EXTENSION_BUILD_DIR"
  npm install --no-package-lock
  ZAP_LSP_BINARY="$BUILD_DIR/zap-lsp" npm run package
)
test -f "$EXTENSION_BUILD_DIR/$EXTENSION_VSIX"

rm -rf "$STAGE_DIR"
mkdir -p "$STAGE_DIR"

echo "Staging files to $STAGE_DIR..."
install -m 755 "$BUILD_DIR/zapc" "$STAGE_DIR/zapc"
install -m 755 "$BUILD_DIR/zap-lsp" "$STAGE_DIR/zap-lsp"
install -m 644 "$BUILD_DIR/stdlib.o" "$STAGE_DIR/stdlib.o"
cp -R "$SCRIPT_DIR/std" "$STAGE_DIR/std"
install -m 644 "$EXTENSION_BUILD_DIR/$EXTENSION_VSIX" \
    "$STAGE_DIR/$EXTENSION_VSIX"

echo "Creating archive $TAR_FILE..."
tar -czf "$TAR_FILE" -C "$SCRIPT_DIR" "$(basename "$STAGE_DIR")"

echo "Release build successful! Archive created at: $TAR_FILE"
