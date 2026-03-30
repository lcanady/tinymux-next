#!/bin/sh
# Print first argument in uppercase — used by execscript tests.
printf '%s\n' "$1" | tr '[:lower:]' '[:upper:]'
