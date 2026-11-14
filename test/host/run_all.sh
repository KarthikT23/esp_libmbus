#!/usr/bin/env bash
# Host tests with gcc on Linux, same sources and flags as in the README.
# SAN=1 adds ASan + UBSan. Output goes to $BUILD (default build_host).
set -u
cd "$(dirname "$0")/../.."

CC=${CC:-gcc}
BUILD=${BUILD:-build_host}
mkdir -p "$BUILD"

CFLAGS="-std=gnu99 -g -Wall -Wextra -Wno-pointer-sign -Iinclude -Itest/host/stubs -Itest/host -Isrc"
if [ "${SAN:-}" = "1" ]; then
    CFLAGS="$CFLAGS -O1 -fsanitize=address,undefined -fno-sanitize-recover=all -fno-omit-frame-pointer"
fi

RECORD=src/mbus-meter-record.c
FRAME=src/mbus-meter-frame.c
PROTO=src/mbus-protocol.c
AUX=src/mbus-protocol-aux.c
LINK=src/mbus-link.c
METER=src/mbus-meter.c
FAKE="test/host/fake_slave.c test/host/host_stubs.c test/host/host_port.c"

WARN_LOG="$BUILD/warnings.log"
: > "$WARN_LOG"
fails=0

# build <name> <sources...>
build() {
    local name=$1; shift
    if ! $CC $CFLAGS "$@" -o "$BUILD/$name" -lm 2>>"$WARN_LOG"; then
        echo "BUILD FAILED: $name"
        tail -n 30 "$WARN_LOG"
        fails=$((fails + 1))
        return 1
    fi
}

# run <name> [args...]
run() {
    local name=$1; shift
    if "$BUILD/$name" "$@" >"$BUILD/$name.log" 2>&1; then
        echo "ok   $name: $(grep -E 'checks' "$BUILD/$name.log" | tail -n 1)"
    else
        echo "FAIL $name"
        tail -n 40 "$BUILD/$name.log"
        fails=$((fails + 1))
    fi
}

build meter_record_test test/host/meter_record_test.c $RECORD && run meter_record_test

build meter_frame_test test/host/meter_frame_test.c $FRAME $RECORD $PROTO \
    && run meter_frame_test test/corpus test/test-frames test/error-frames test/unsupported-frames

build meter_read_test test/host/meter_read_test.c $FAKE $METER $RECORD $FRAME $LINK $PROTO $AUX \
    && run meter_read_test

build meter_scan_test test/host/meter_scan_test.c $FAKE $METER $RECORD $FRAME $LINK $PROTO $AUX \
    && run meter_scan_test

build fake_slave_test test/host/fake_slave_test.c $FAKE $PROTO $AUX $LINK \
    && run fake_slave_test

build decode_example_test test/host/decode_example_test.c $RECORD $FRAME $PROTO $AUX $METER $LINK $FAKE \
    && run decode_example_test

# randomised decode, fixed seeds. The sanitizer run does more work.
CORPUS_DIRS="test/corpus test/test-frames test/error-frames test/unsupported-frames"
build fuzz_decode test/host/fuzz_decode.c $FRAME $RECORD $PROTO
if [ "${SAN:-}" = "1" ]; then
    for seed in 1 2 3; do
        run fuzz_decode -l -n 1500 -s $seed $CORPUS_DIRS
    done
else
    run fuzz_decode -l -n 300 $CORPUS_DIRS
fi

# docs: every snippet compiles, the offline example output is true
if python3 test/host/check_docs.py; then :; else
    fails=$((fails + 1))
fi

# upstream sources keep their warnings, ours must stay clean
if grep -E '^(src/mbus-(meter|link)|include/mbus-(meter|link)|test/host/)[^:]*:[0-9]+:[0-9]+: warning' "$WARN_LOG"; then
    echo "FAIL: warnings in our own sources"
    fails=$((fails + 1))
fi

if [ "$fails" -ne 0 ]; then
    echo "$fails problem(s)"
    exit 1
fi
echo "all host tests passed"
