#!/bin/sh

PATH=/bin:/usr/bin
TERM=screen
LC_ALL=C.UTF-8
LANG=C.UTF-8
export TERM LC_ALL LANG

[ -z "$TEST_TMUX" ] && TEST_TMUX=$(readlink -f ../tmux)
OUT=$(mktemp -d)
TMUX_TMPDIR="$OUT"
export TMUX_TMPDIR
TMUX="$TEST_TMUX -LtestA$$ -f/dev/null"

fail()
{
	echo "$*" >&2
	$TMUX kill-server 2>/dev/null || true
	rm -rf "$OUT"
	exit 1
}

cleanup()
{
	$TMUX kill-server 2>/dev/null || true
	rm -rf "$OUT"
}
trap cleanup EXIT

wait_channel()
{
	channel=$1

	if command -v timeout >/dev/null 2>&1; then
		timeout 10 $TMUX wait-for "$channel" ||
			fail "wait-for $channel timed out"
		return
	fi

	$TMUX wait-for "$channel" &
	pid=$!
	i=0
	while kill -0 "$pid" 2>/dev/null; do
		[ $i -lt 50 ] || {
			kill "$pid" 2>/dev/null || true
			fail "wait-for $channel timed out"
		}
		i=$((i + 1))
		sleep 0.2
	done
	wait "$pid" || fail "wait-for $channel failed"
}

assert_unchanged()
{
	option=$1
	expected=$2
	count=${3:-15}
	i=0

	while [ $i -lt "$count" ]; do
		value=$($TMUX show -gqv "$option" 2>/dev/null || true)
		[ "$value" = "$expected" ] ||
			fail "expected $option to remain '$expected' but got '$value'"
		i=$((i + 1))
		sleep 0.2
	done
}

$TMUX new -d -s wf || fail "new-session failed"

$TMUX set -g @wf_value 0 || fail "set @wf_value failed"
$TMUX set-hook -g -B '@wf::#{@wf_value}' 'wait-for -S wf-hook' ||
	fail "set-hook -B failed"

$TMUX wait-for -E @wf \; wait-for -S wf-event &
event_pid=$!

# Let the monitor take its first sample so the next change is reported.
sleep 1.5

$TMUX set -g @wf_value 1 || fail "set @wf_value 1 failed"

wait_channel wf-event
wait_channel wf-hook
wait "$event_pid" || fail "wait-for -E command failed"

$TMUX set -g @late 0 || fail "set @late failed"
$TMUX wait-for -E @wf \; set -g @late 1 \; wait-for -S wf-late &
late_pid=$!
assert_unchanged @late 0 5

$TMUX set -g @wf_value 2 || fail "set @wf_value 2 failed"
wait_channel wf-late
wait "$late_pid" || fail "late wait-for -E command failed"

$TMUX new -d -s wf2 || fail "new-session wf2 failed"

$TMUX wait-for -E window-renamed \; wait-for -S wf-renamed &
renamed_pid=$!

sleep 0.5
$TMUX rename-window -t wf2:0 renamed || fail "rename-window failed"
wait_channel wf-renamed
wait "$renamed_pid" || fail "wait-for -E window-renamed failed"

$TMUX set-hook -g window-renamed 'wait-for -S wf-hook-renamed' ||
	fail "set-hook window-renamed failed"
$TMUX rename-window -t wf2:0 renamed-again ||
	fail "rename-window renamed-again failed"
wait_channel wf-hook-renamed

exit 0
