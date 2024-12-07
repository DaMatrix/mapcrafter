#!/bin/bash

CWD=$(dirname $(realpath ${BASH_SOURCE[0]}))

# Wait for the install of nginx
while [[ ! -f "/etc/init.d/nginx" ]]; do
  echo Wait for Nginx installation
  sleep 4
done

# File found, so wait a bit to be sure
echo Starting Nginx
sleep 4

# Start nginx
/etc/init.d/nginx start
