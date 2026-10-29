#!/usr/bin/env bash

# sets up the IDF repo incl submodules with specified version as $1
set -o errexit # Exit if command failed.

if [ -z $IDF_PATH ] || [ -z $IDF_VERSION ] ; then
    echo "Mandatory variables undefined"
    exit 1;
fi;

cd $IDF_PATH
# Cleans out the untracked files in the repo, so the next "git checkout" doesn't fail
git clean -f

if [[ "$IDF_TAG_FLAG" = "false" ]]; then
    git fetch origin ${IDF_VERSION}:${IDF_VERSION} --depth 1
    git checkout ${IDF_VERSION}
    echo "The IDF branch is "${IDF_VERSION}
else
    git fetch origin tag ${IDF_VERSION} --depth 1
    git checkout ${IDF_VERSION}
    echo "The IDF branch is TAG:"${IDF_VERSION}
fi

cmake --version
