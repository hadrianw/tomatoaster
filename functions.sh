#!/bin/false
newest_in_dir()
{
	local dir="$1"
	local newest=""
	local except="$2"

	for i in "$dir"/*; do
		case "$i" in
			$except) ;;
			*) test "$i" -nt "$newest" && newest="$i" ;;
		esac
	done
	echo "$newest"
}
