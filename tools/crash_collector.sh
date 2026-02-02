#!/bin/bash
# Crash log collector for N64 development
# Monitors stdin for crash patterns and RDP validation messages
# Usage: some_command 2>&1 | ./crash_collector.sh

CRASH_LOG="${CRASH_LOG_FILE:-crash_logs.txt}"
RDP_LOG="${RDP_LOG_FILE:-rdp_logs.txt}"
TIMEOUT_SECONDS="${CRASH_TIMEOUT:-3}"

# State tracking
in_crash=false
crash_buffer=""
crash_count=0
rdp_count=0

# Cleanup on exit
cleanup() {
    if $in_crash && [ -n "$crash_buffer" ]; then
        flush_crash
    fi
    echo "" >&2
    if [ $crash_count -gt 0 ]; then
        echo "[CRASH COLLECTOR] Session ended. Total crashes captured: $crash_count" >&2
        echo "[CRASH COLLECTOR] View crash logs: cat $CRASH_LOG" >&2
    fi
    if [ $rdp_count -gt 0 ]; then
        echo "[CRASH COLLECTOR] Total RDP validation messages: $rdp_count" >&2
        echo "[CRASH COLLECTOR] View RDP logs: cat $RDP_LOG" >&2
    fi
}
trap cleanup EXIT

# Function to flush crash buffer to file
flush_crash() {
    if [ -n "$crash_buffer" ]; then
        ((crash_count++)) || true
        {
            echo ""
            echo "================================================================================"
            echo "CRASH #$crash_count CAPTURED AT: $(date '+%Y-%m-%d %H:%M:%S')"
            echo "================================================================================"
            echo "$crash_buffer"
            echo ""
        } >> "$CRASH_LOG"
        echo -e "\n[CRASH COLLECTOR] Crash #$crash_count saved to $CRASH_LOG" >&2
        crash_buffer=""
    fi
    in_crash=false
}

# Function to log RDP validation message
log_rdp_message() {
    local line="$1"
    ((rdp_count++)) || true
    {
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] $line"
    } >> "$RDP_LOG"
    # Show first few RDP messages in terminal, then just count
    if [ $rdp_count -le 5 ]; then
        echo -e "\033[33m[RDP] $line\033[0m" >&2
    elif [ $rdp_count -eq 6 ]; then
        echo -e "\033[33m[RDP] (further messages logged to $RDP_LOG)\033[0m" >&2
    fi
}

# Check if a line is an RDP validation message
is_rdp_message() {
    local line="$1"
    [[ "$line" == *"[RDPQ]"* ]] || \
    [[ "$line" == *"[RDPQ_VALIDATION]"* ]] || \
    [[ "$line" == *"RDPQ:"* ]] || \
    [[ "$line" == *"rdpq_debug"* ]] || \
    [[ "$line" == *"RDP validation"* ]] || \
    [[ "$line" == *"VALIDATE"* ]] || \
    [[ "$line" == *"RDP error"* ]] || \
    [[ "$line" == *"RDP warning"* ]] || \
    [[ "$line" == *"invalid RDP"* ]] || \
    [[ "$line" == *"RDP state"* ]] || \
    [[ "$line" == *"RDPQ_VALIDATE"* ]] || \
    [[ "$line" == *"rdpq:"* ]] || \
    [[ "$line" == *"SET_OTHER_MODES"* ]] || \
    [[ "$line" == *"TRI_TEX"* ]] || \
    [[ "$line" == *"TRI_SHADE"* ]] || \
    [[ "$line" == *"non-shaded primitive"* ]] || \
    [[ "$line" == *"SHADE_ALPHA"* ]] || \
    [[ "$line" == *"blender"* && "$line" == *"slot"* ]] || \
    [[ "$line" == *"SOM:"* ]] || \
    [[ "$line" == *"CC:"* && "$line" == *"mode"* ]] || \
    [[ "$line" == *"TEXEL"* ]] || \
    [[ "$line" == *"TMEM"* ]] || \
    [[ "$line" == *"combiner"* && "$line" == *"invalid"* ]] || \
    [[ "$line" == *"texture"* && "$line" == *"invalid"* ]] || \
    [[ "$line" == *"triangle"* && "$line" == *"invalid"* ]]
}

# Check if a line starts a new crash
is_crash_start() {
    local line="$1"
    [[ "$line" == *"******* CPU EXCEPTION *******"* ]] || \
    [[ "$line" == *"RSP CRASH |"* ]] || \
    [[ "$line" == *"RSP CRASH"* ]] || \
    [[ "$line" == *"ASSERTION FAILED"* ]] || \
    [[ "$line" == *"Exception:"* ]] || \
    [[ "$line" == *"Panic:"* ]] || \
    [[ "$line" == *"FATAL:"* ]] || \
    [[ "$line" == *"CPU Exception"* ]]
}

# Check if a line is part of crash data (backtrace, registers, etc.)
is_crash_continuation() {
    local line="$1"
    # Backtrace markers
    [[ "$line" == *"backtrace:"* ]] || \
    [[ "$line" == *"Backtrace:"* ]] || \
    [[ "$line" == *"Stack trace:"* ]] || \
    # Stack frame lines (indented with address in brackets)
    [[ "$line" =~ ^[[:space:]]+.*\[0x[0-9a-fA-F]+\] ]] || \
    # Register dumps
    [[ "$line" =~ ^[[:space:]]*(pc|ra|sp|gp|fp|at|v[0-9]|a[0-9]|t[0-9]|s[0-9]|k[0-9]): ]] || \
    [[ "$line" == *"PC:"* ]] || \
    [[ "$line" == *"RA:"* ]] || \
    [[ "$line" == *"SP:"* ]] || \
    # Exception info
    [[ "$line" == *"Cause:"* ]] || \
    [[ "$line" == *"Status:"* ]] || \
    [[ "$line" == *"EPC:"* ]] || \
    [[ "$line" == *"BadVAddr:"* ]] || \
    # File/line info
    [[ "$line" == *'file "'* ]] || \
    [[ "$line" == *"line "* ]] || \
    [[ "$line" == *"function:"* ]] || \
    # Memory addresses and hex dumps
    [[ "$line" =~ ^[[:space:]]*0x[0-9a-fA-F]+ ]] || \
    # Libdragon inspector output
    [[ "$line" == *"inspector"* ]] || \
    [[ "$line" == *"EXCEPTION HANDLER"* ]] || \
    [[ "$line" == *"INVALID ADDRESS"* ]] || \
    [[ "$line" == *"interrupted because"* ]] || \
    # Generic crash continuation (indented lines or lines with ???)
    [[ "$line" == *"???"* ]] || \
    [[ "$line" =~ ^\  ]] || \
    # Empty lines during crash (keep them for formatting)
    [[ -z "${line// }" ]]
}

# Main loop - read with timeout
while true; do
    if IFS= read -r -t "$TIMEOUT_SECONDS" line; then
        # Got a line - pass it through to display
        printf '%s\n' "$line"

        # Check for RDP validation messages first
        if is_rdp_message "$line"; then
            log_rdp_message "$line"
        fi

        # Check for crash start patterns
        if is_crash_start "$line"; then
            # If we were already in a crash, flush the previous one
            if $in_crash && [ -n "$crash_buffer" ]; then
                flush_crash
            fi
            in_crash=true
            crash_buffer="$line"
        elif $in_crash; then
            # Check if this line is part of the crash
            if is_crash_continuation "$line"; then
                crash_buffer="$crash_buffer"$'\n'"$line"
            else
                # Non-crash line - but could be interspersed debug output
                # Keep collecting for a bit more
                crash_buffer="$crash_buffer"$'\n'"$line"
            fi
        fi
    else
        read_status=$?

        # Timeout (status 142 on most systems, or >128)
        if [ $read_status -gt 128 ]; then
            # Timeout - flush any pending crash
            if $in_crash; then
                flush_crash
            fi
            # Continue waiting for more input
            continue
        fi

        # EOF or error (status 1) - exit the loop
        break
    fi
done
