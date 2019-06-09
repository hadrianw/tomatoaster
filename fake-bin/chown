#!/bin/sh
set -e

echo CHOWN $@

for x in "$@"; do
	case $x in
	-R)
		recursive="-R"
		;;
	-*)
		echo "CHOWN: Unsupported flag \"$x\""
		exit 1
		;;
	esac
done

for x in "$@"; do
	case $x in
	-*)
		;;
	*)
		if [ -z "$spec" ]; then
			spec="$x"
			continue
		fi
		chgrp "$recursive" "${spec#*:}" "$x"
		;;
	esac
done
