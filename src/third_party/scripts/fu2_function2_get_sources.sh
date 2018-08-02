#!/usr/bin/env bash

set -o verbose
set -o errexit

# This script downloads and imports Naios's function2 library.
# This is a header-only library.
# This script is designed to run on most unix-like OSes

VERSION=3.0.0
TMP_BRANCH=v${VERSION}
VERSION_TAG=${VERSION}
NAME=function2
DEST_DIR=$(git rev-parse --show-toplevel)/src/third_party/${NAME}-${VERSION}
REPO_URI=https://github.com/naios/function2
HEADER_PATH=include/function2/function2.hpp
GIT_DIR=${NAME}-repo

rm -fr ${GIT_DIR}
rm -fr ${DEST_DIR}

git clone ${REPO_URI} ${GIT_DIR}

cd ${GIT_DIR}

echo ${VERSION_TAG}
git checkout -b ${TMP_BRANCH} ${VERSION_TAG}

mkdir ${DEST_DIR}
cp ${HEADER_PATH} ${DEST_DIR}
cp LICENSE.txt ${DEST_DIR}

cd ..

# Clean up git repository.
rm -fr ${GIT_DIR}

# Note: As this is header only, we just leave the header there.
echo "Done"
