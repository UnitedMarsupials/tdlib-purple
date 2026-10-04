#!/bin/sh
#
# Write this job's outcomes to outcome.tsv, one line, for .github/summary.sh:
#
#	ORDER  NAME  setup  configure  build  test  install
#
# SETUP holds the outcomes of every preparatory step (boot, packages); the
# first that is not "success" speaks for all of them.  The others are the
# outcome of one step each: success, failure, cancelled or skipped -- or
# "n/a", for a job that has no such step.

setup=success
for o in $SETUP; do
	[ "$o" = success ] || { setup=$o; break; }
done

printf '%s\t%s\t%s\t%s\t%s\t%s\t%s\n' "$ORDER" "$NAME" "$setup" \
    "${CONFIGURE:-skipped}" "${BUILD:-skipped}" "${TEST:-skipped}" \
    "${INSTALL:-skipped}" > outcome.tsv
