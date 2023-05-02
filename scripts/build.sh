#!/bin/bash
set -euo pipefail
trap 'echo "script error: $(basename "$BASH_SOURCE"):$LINENO $BASH_COMMAND" >&2' ERR
script_dir="$(dirname -- "$(readlink -f -- "$0")")"
#script_dir="$(cd -- "$(dirname -- "$0")" && pwd)"

error () {
	echo "Error: $@" >&1
	exit 1
}

print-help () {
cat >&2 <<EOF
Usage: $(basename "$0") [OPTION...]

OPTIONS
   -c, --clean			Perform full rebuild
   -d, --debug			Build with debugging symbols and address sanitizer
   -h, --help			Display this help and exit.
EOF
exit 1
}

clean=""
build_type="Release"

for i in "$@"; do
case $i in
	-c|--clean)
	clean="1"
	;;
	-d|--debug)
	build_type="Debug"
	;;
	-h|--help)
	print-help
	;;

	*)
	error "Unknown argument: $i (--help for usage)"
	;;
esac
done

root_dir="$script_dir/.."
build_dir="$root_dir/build"

mkdir -p "$build_dir" && cd "$build_dir"
[ -n "$clean" ] && make clean
cmake -DCMAKE_BUILD_TYPE="$build_type" .. && make -j"$(nproc)"
