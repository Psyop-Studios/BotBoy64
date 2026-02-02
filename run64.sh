#!/bin/bash
# Build and deploy to SummerCart64 with crash and RDP log collection

set -e  # Exit on error

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRASH_LOG="$SCRIPT_DIR/crash_logs.txt"
RDP_LOG="$SCRIPT_DIR/rdp_logs.txt"
COLLECTOR="$SCRIPT_DIR/tools/crash_collector.sh"

echo "=== Clean Build ==="
make clean

echo ""
echo "=== Compiling ==="
make

echo ""
echo "=== Deploying to SummerCart64 ==="
sc64deployer upload game.z64

echo ""
echo "=== Initializing Log Collectors ==="
# Delete and recreate log files
rm -f "$CRASH_LOG"
rm -f "$RDP_LOG"
touch "$CRASH_LOG"
touch "$RDP_LOG"
echo "Crash logs will be saved to: $CRASH_LOG"
echo "RDP validation logs will be saved to: $RDP_LOG"
echo "Patterns detected: '******* CPU EXCEPTION *******', 'RSP CRASH |', 'ASSERTION FAILED', 'Exception:', '[RDPQ_VALIDATION]'"

echo ""
echo "=== Debug Output (Ctrl+C to exit) ==="
echo "Crash/RDP logs are automatically captured after 1 second of no output following a pattern."
echo "Debug listener will auto-reconnect on N64 power cycle."
echo ""

# Make collector executable if not already
chmod +x "$COLLECTOR" 2>/dev/null || true

# Run debug with crash collection in a loop to survive power cycles
export CRASH_LOG_FILE="$CRASH_LOG"
export RDP_LOG_FILE="$RDP_LOG"

while true; do
    # Wait for SC64 to be available
    echo "[run64.sh] Waiting for SummerCart64..."
    until sc64deployer info >/dev/null 2>&1; do
        sleep 1
    done

    echo "[run64.sh] Connected! Starting debug listener..."

    # Use stdbuf to disable buffering for real-time output
    sc64deployer debug 2>&1 | stdbuf -oL "$COLLECTOR" || true

    echo ""
    echo "[run64.sh] Debug connection lost. Power cycle N64 to reconnect, or Ctrl+C to exit."
done
