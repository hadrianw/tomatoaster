#!/bin/sh
set -e

echo CHOWN $@

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
			find "$x" >> "config/chown/$spec"
		else
			echo "$x" >> "config/chown/$spec"
		fi
		;;
	esac
done
