#!/bin/sh
#
# annotate.sh TITLE COMMAND...
#
# Run COMMAND, show its output, and if it fails, repeat the last lines of that
# output as a GitHub Actions error annotation titled TITLE.  Annotations head
# the run's summary page, so a failure can be read without opening the log --
# and from the API, which serves annotations but not logs.

title=$1
shift

log=$(mktemp)
rc=0
"$@" > "$log" 2>&1 || rc=$?
cat "$log"

if [ "$rc" != 0 ] && [ -n "${GITHUB_ACTIONS:-}" ]; then
	# Workflow commands take %, CR and LF escaped, in that order.
	tail -n 40 "$log" | awk -v t="$title (exit $rc)" '
	{ gsub(/%/, "%25"); gsub(/\r/, "%0D"); msg = msg (NR > 1 ? "%0A" : "") $0 }
	END { print "::error title=" t "::" msg }'
fi
rm -f "$log"
exit $rc
