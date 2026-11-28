#!/bin/sh
#
# summary.sh DIR -- one Markdown table of every job's outcomes, from the
# outcome.tsv files that .github/record.sh left under DIR.

dir=$1

mark() {
	case $1 in
	success)	printf '✅ passed' ;;
	failure)	printf '❌ **failed**' ;;
	cancelled)	printf '⚪ cancelled' ;;
	skipped)	printf '➖ skipped' ;;
	n/a)		printf '' ;;
	*)		printf '%s' "$1" ;;
	esac
}

echo '## Build and test'
echo
echo '| Job | Setup | Configure | Build | Test | Valgrind | Sanitizers | Style | Install |'
echo '| --- | --- | --- | --- | --- | --- | --- | --- | --- |'

for f in "$dir"/*/outcome.tsv; do
	[ -f "$f" ] && cat "$f"
done | sort | while IFS='	' read -r order name setup configure build test install valgrind sanitize style; do
	printf '| %s | %s | %s | %s | %s | %s | %s | %s | %s |\n' "$name" \
	    "$(mark "$setup")" "$(mark "$configure")" "$(mark "$build")" \
	    "$(mark "$test")" "$(mark "$valgrind")" "$(mark "$sanitize")" \
	    "$(mark "$style")" "$(mark "$install")"
done

if ! ls "$dir"/*/outcome.tsv >/dev/null 2>&1; then
	echo '| (no job recorded an outcome) | | | | | | | | |'
fi

echo
echo 'Setup is booting the VM and installing packages; Configure is' \
    '`cmake`; Build is `ninja` for the plugin and for the test binary; Test' \
    'is `ninja run-tests` (for the translations, `po/lint_accelerators.py`);' \
    'Valgrind is the same tests again under Valgrind, where it is available;' \
    'Sanitizers is a build with AddressSanitizer and UBSan, tested again;' \
    'Style is clang-tidy with the checks of `.clang-tidy`, in one job;' \
    'Install is `ninja install` into a scratch `DESTDIR`.  Jobs marked' \
    '*experimental* do not fail the run.'
