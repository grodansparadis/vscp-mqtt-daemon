#!/usr/bin/env bash

set -euo pipefail

version_file=${1:-src/version.h}
year=$(date -u +%y)
month=$(date -u +%m)
patch="${VSCPD_BUILD_NUMBER:-${GITHUB_RUN_NUMBER:-$(git rev-list --count --first-parent HEAD)}}"
version="${year}.${month}.${patch}"

perl -pi -e "s/(MQTTVSCPD_VERSION_MAJOR\s+)\d+/\${1}${year}/" "${version_file}"
perl -pi -e "s/(MQTTVSCPD_VERSION_MINOR\s+)\d+/\${1}${month}/" "${version_file}"
perl -pi -e "s/(MQTTVSCPD_VERSION_PATCH\s+)\d+/\${1}${patch}/" "${version_file}"
perl -pi -e "s/(MQTTVSCPD_VERSION_STRING\s+).*/\${1}\"${version}\"/" "${version_file}"
perl -pi -e "s/(MQTTVSCPD_DISPLAY_VERSION\s+).*/\${1}\"${version}\"/" "${version_file}"

# Expose the version to GitHub Actions steps
if [ -n "${GITHUB_ENV:-}" ]; then
  echo "VSCPD_VERSION=${version}" >> "${GITHUB_ENV}"
fi
