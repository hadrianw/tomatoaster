#!/bin/sh
set -e

echo CHGRP $@

for x in "$@"; do
	case $x in
	-R)
		recursive=yes
		;;
	-*)
		echo "Unsupported flag \"$x\""
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
		if [ "$recursive" = "yes" -a -d "$x" ]; then
			find "$x" >> "config/chgrp/$spec"
		else
			echo "$x" >> "config/chgrp/$spec"
		fi
		;;
	esac
done
