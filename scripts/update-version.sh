#!/usr/bin/env bash

set -euo pipefail

version_file=${1:-src/version.h}
year=$(date -u +%y)
month=$(date -u +%m)
patch=$(git rev-list --count --first-parent HEAD)
version="${year}.${month}.${patch}"

sed -i -E "s/(MQTTVSCPD_VERSION_MAJOR[[:space:]]+)[0-9]+/\1${year}/" "${version_file}"
sed -i -E "s/(MQTTVSCPD_VERSION_MINOR[[:space:]]+)[0-9]+/\1${month}/" "${version_file}"
sed -i -E "s/(MQTTVSCPD_VERSION_PATCH[[:space:]]+)[0-9]+/\1${patch}/" "${version_file}"
sed -i -E "s/(MQTTVSCPD_VERSION_STRING[[:space:]]+).*/\1\"${version}\"/" "${version_file}"
sed -i -E "s/(MQTTVSCPD_DISPLAY_VERSION[[:space:]]+).*/\1\"${version}\"/" "${version_file}"