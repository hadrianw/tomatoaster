#!/bin/sh
set -e

getid() {
	awk -v name="$2" -F: '
	BEGIN {
		if(name ~ /^[0-9]+$/) {
			print name
			exit 0
		}
	}
	$1 == name {
		print $3
		exit 0
	}
	END {
		exit 1
	}' "etc/$1"
}

echo CHGRP $@

for x in "$@"; do
	case $x in
	-R)
		recursive=yes
		;;
	-*)
		echo "CHGRP: Unsupported flag \"$x\"" > /dev/stderr
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
			find "$x" -exec setfattr -h -n user.escape.gid -v "\"$(getid group "$spec")\"" '{}' \;
		else
			setfattr -h -n user.escape.gid -v "\"$(getid group "$spec")\"" "$x"
		fi
		;;
	esac
done
