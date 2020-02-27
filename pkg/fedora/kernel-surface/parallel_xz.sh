#!/bin/sh
# Reads filenames on stdin, xz-compresses each in place.
# Not optimal for "compress relatively few, large files" scenario!

# How many xz's to run in parallel:
procgroup=""
while test "$#" != 0; do
	# Get it from -jNUM
	N="${1#-j}"
	if test "$N" = "$1"; then
		# Not -j<something> - warn and ignore
		echo "parallel_xz: warning: unrecognized argument: '$1'"
	else
		procgroup="$N"
	fi
	shift
done

# If told to use only one cpu:
test "$procgroup" || exec xargs -r xz
test "$procgroup" = 1 && exec xargs -r xz

# xz has some startup cost. If files are really small,
# this cost might be significant. To combat this,
# process several files (in sequence) by each xz process via -n 16:
exec xargs -r -n 16 -P $procgroup xz
