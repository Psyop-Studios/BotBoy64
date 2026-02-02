#!/bin/bash
# Stress test script - runs emulator multiple times to catch intermittent crashes
# Usage: ./tools/stress_test.sh [num_runs] [timeout_seconds]

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
ROM="$PROJECT_DIR/game.z64"
CRASH_LOG="$PROJECT_DIR/crash_logs.txt"
RDP_LOG="$PROJECT_DIR/rdp_logs.txt"
STRESS_LOG="$PROJECT_DIR/stress_test_results.txt"
COLLECTOR="$SCRIPT_DIR/crash_collector.sh"

NUM_RUNS="${1:-10}"
TIMEOUT="${2:-600}"  # 10 minutes default
EMULATOR="${N64_EMULATOR:-ares}"

echo "=== N64 Stress Test ==="
echo "Runs: $NUM_RUNS"
echo "Timeout per run: ${TIMEOUT}s ($(($TIMEOUT / 60)) min)"
echo "Emulator: $EMULATOR"
echo ""

# Build first
echo "=== Building ROM ==="
cd "$PROJECT_DIR"
make -j$(nproc)

if [ ! -f "$ROM" ]; then
    echo "Error: ROM not found!"
    exit 1
fi

# Initialize logs
rm -f "$CRASH_LOG" "$RDP_LOG" "$STRESS_LOG"
touch "$CRASH_LOG" "$RDP_LOG" "$STRESS_LOG"

echo "Stress test started at $(date)" >> "$STRESS_LOG"
echo "Emulator: $EMULATOR, Runs: $NUM_RUNS, Timeout: ${TIMEOUT}s" >> "$STRESS_LOG"
echo "" >> "$STRESS_LOG"

crash_count=0
successful_runs=0

for i in $(seq 1 $NUM_RUNS); do
    echo ""
    echo "=== Run $i/$NUM_RUNS ==="

    run_start=$(date +%s)

    export CRASH_LOG_FILE="$CRASH_LOG"
    export RDP_LOG_FILE="$RDP_LOG"

    # Run emulator with timeout, pipe through crash collector
    set +e  # Don't exit on error

    case "$EMULATOR" in
        ares)
            # ares doesn't output debug info, just run with timeout
            timeout "$TIMEOUT" ares "$ROM" 2>&1 | stdbuf -oL "$COLLECTOR" &
            EMU_PID=$!
            ;;
        cen64)
            timeout "$TIMEOUT" cen64 -is-viewer -rom "$ROM" 2>&1 | stdbuf -oL "$COLLECTOR" &
            EMU_PID=$!
            ;;
        simple64)
            timeout "$TIMEOUT" simple64-gui "$ROM" 2>&1 | stdbuf -oL "$COLLECTOR" &
            EMU_PID=$!
            ;;
        *)
            timeout "$TIMEOUT" $EMULATOR "$ROM" 2>&1 | stdbuf -oL "$COLLECTOR" &
            EMU_PID=$!
            ;;
    esac

    # Wait for emulator to finish or timeout
    wait $EMU_PID 2>/dev/null
    exit_code=$?

    set -e

    run_end=$(date +%s)
    run_duration=$((run_end - run_start))

    # Check if crash was captured
    crashes_before=$crash_count
    crash_count=$(grep -c "CRASH #" "$CRASH_LOG" 2>/dev/null || echo "0")

    if [ "$crash_count" -gt "$crashes_before" ]; then
        new_crashes=$((crash_count - crashes_before))
        echo "  -> CRASHED! ($new_crashes new crash(es))"
        echo "Run $i: CRASH (${run_duration}s, exit=$exit_code, +$new_crashes crashes)" >> "$STRESS_LOG"
    elif [ "$exit_code" -eq 124 ]; then
        echo "  -> Timeout (ran full ${TIMEOUT}s without crash)"
        echo "Run $i: TIMEOUT (${run_duration}s)" >> "$STRESS_LOG"
        ((successful_runs++)) || true
    else
        echo "  -> Completed (exit code: $exit_code)"
        echo "Run $i: OK (${run_duration}s, exit=$exit_code)" >> "$STRESS_LOG"
        ((successful_runs++)) || true
    fi

    # Small delay between runs
    sleep 1
done

echo ""
echo "=== Stress Test Complete ==="
echo "Total runs: $NUM_RUNS"
echo "Successful/Timeout: $successful_runs"
echo "Crashes captured: $crash_count"
echo ""
echo "Results saved to: $STRESS_LOG"
echo "Crash logs: $CRASH_LOG"
echo "RDP logs: $RDP_LOG"

# Summary
echo "" >> "$STRESS_LOG"
echo "=== Summary ===" >> "$STRESS_LOG"
echo "Total runs: $NUM_RUNS" >> "$STRESS_LOG"
echo "Successful/Timeout: $successful_runs" >> "$STRESS_LOG"
echo "Crashes captured: $crash_count" >> "$STRESS_LOG"
echo "Completed at $(date)" >> "$STRESS_LOG"

# Show crash summary if any
if [ "$crash_count" -gt 0 ]; then
    echo ""
    echo "=== Crash Summary ==="
    grep -E "CRASH #|exception at PC:|Exception address:" "$CRASH_LOG" | head -50
fi
