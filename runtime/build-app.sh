
set -eu

if [ "$#" -ne 2 ]; then
    echo "usage: $0 <source.c> <output>"
    exit 1
fi

SOURCE="$1"
OUTPUT="$2"

ROOT="$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)"
RUNTIME="$ROOT/runtime"
BUILD="$ROOT/.runtime-build"

mkdir -p "$BUILD"

clang \
    -target x86_64-unknown-elf \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -mno-red-zone \
    -mno-sse \
    -mno-sse2 \
    -nostdlib \
    -nodefaultlibs \
    -O2 \
    -Wall \
    -Wextra \
    -I"$RUNTIME" \
    -c "$SOURCE" \
    -o "$BUILD/app.o"

clang \
    -target x86_64-unknown-elf \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -mno-red-zone \
    -mno-sse \
    -mno-sse2 \
    -nostdlib \
    -nodefaultlibs \
    -I"$RUNTIME" \
    -c "$RUNTIME/runtime_entry.S" \
    -o "$BUILD/runtime_entry.o"

clang \
    -target x86_64-unknown-elf \
    -ffreestanding \
    -fno-builtin \
    -fno-stack-protector \
    -mno-red-zone \
    -mno-sse \
    -mno-sse2 \
    -nostdlib \
    -nodefaultlibs \
    -I"$RUNTIME" \
    -c "$RUNTIME/nicos_runtime.c" \
    -o "$BUILD/nicos_runtime.o"

ld.lld \
    -T "$RUNTIME/runtime.ld" \
    -o "$BUILD/app.elf" \
    "$BUILD/runtime_entry.o" \
    "$BUILD/app.o" \
    "$BUILD/nicos_runtime.o"

llvm-objcopy \
    -O binary \
    "$BUILD/app.elf" \
    "$OUTPUT"

echo "Built: $OUTPUT"
