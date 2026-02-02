#!/bin/bash
set -e

# Configuration
ROM_NAME="game.z64"
EMULATOR="${N64_EMULATOR:-ares}"
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
CRASH_LOG="$SCRIPT_DIR/crash_logs.txt"
RDP_LOG="$SCRIPT_DIR/rdp_logs.txt"
COLLECTOR="$SCRIPT_DIR/tools/crash_collector.sh"

make clean
# Clean and build
echo "Building $ROM_NAME..."
make -j$(nproc)

# Check if ROM was created
if [ ! -f "$ROM_NAME" ]; then
    echo "Error: ROM file $ROM_NAME not found!"
    exit 1
fi

echo "Build successful!"

# Initialize crash log collector
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

# Make collector executable if not already
chmod +x "$COLLECTOR" 2>/dev/null || true
export CRASH_LOG_FILE="$CRASH_LOG"
export RDP_LOG_FILE="$RDP_LOG"

echo ""
echo "Launching in $EMULATOR..."
echo "Crash/RDP logs are automatically captured after 1 second of no output following a pattern."
echo ""

# Launch emulator with crash collection where possible
# Note: Not all emulators output debug info to stdout/stderr
# For best crash logging, use cen64 with -is-viewer option
case "$EMULATOR" in
    ares)
        # ares doesn't typically output debug info to terminal
        # Run normally, crash collection won't capture much
        ares "$ROM_NAME"
        ;;
    mupen64plus)
        # mupen64plus may output some debug info
        mupen64plus "$ROM_NAME" 2>&1 | stdbuf -oL "$COLLECTOR"
        ;;
    simple64)
        simple64-gui "$ROM_NAME"
        ;;
    cen64)
        # cen64 with -is-viewer outputs debug info to stdout
        echo "Note: Use 'N64_EMULATOR=cen64' for best crash logging support"
        cen64 -is-viewer -rom "$ROM_NAME" 2>&1 | stdbuf -oL "$COLLECTOR"
        ;;
    *)
        echo "Unknown emulator: $EMULATOR"
        echo "Supported: ares, mupen64plus, simple64, cen64"
        echo "Trying to run $EMULATOR directly with crash collection..."
        $EMULATOR "$ROM_NAME" 2>&1 | stdbuf -oL "$COLLECTOR"
        ;;
esac
