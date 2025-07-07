#!/bin/sh

if [ $# -ne 1 ]; then
	echo "Usage: '$0 <DIR>'"
	exit 1
fi

dir="$1"
dest="$dir/version.cpp"
dest_temp="$dir/version.cpp.tmp"

if [ ! -d "$dir" ]; then
  mkdir -p "$dir"
fi

mc_version=$(cat ../../MCVERSION)
version=$(cat ../../VERSION)
gitversion=""
if [ -d "../../.git" ]; then
    gitversion=$(git describe)
	version="$version.$(echo $gitversion | tr "-" " " | awk '{print $2}')"
fi

cat > "$dest_temp" <<EOF
namespace mapcrafter {
	const char* MINECRAFT_VERSION = "$mc_version";
	const char* MAPCRAFTER_VERSION = "$version";
	const char* MAPCRAFTER_GITVERSION = "$gitversion";
};
EOF

if [ -f "$dest" ]; then
    if diff "$dest_temp" "$dest" > /dev/null; then
        rm "$dest_temp"
        exit
    fi
fi

mv "$dest_temp" "$dest"
