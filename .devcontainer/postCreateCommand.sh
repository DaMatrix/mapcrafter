#!/bin/bash

CWD=$(dirname $(realpath ${BASH_SOURCE[0]}))

# Make a link to the block files
cp --remove-destination -r /blocks /workspaces/mapcrafter/src/data
# cp --remove-destination -rs /blocks /workspaces/mapcrafter/src/data

# Include dotnet path
# echo "PATH=\"\$PATH:/root/.dotnet\"" >>/etc/environment

# Install missing libs
apt-get install -q=1 -y --no-install-recommends \
    zlib1g-dev \
    libpng-dev \
    libjpeg-turbo8-dev

# Install boost only used components
apt-get install -q=1 -y --no-install-recommends \
    libboost-iostreams-dev \
    libboost-system-dev \
    libboost-filesystem-dev \
    libboost-program-options-dev
