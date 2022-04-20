#!/usr/bin/env bash
# build and install duckdb

set -e

# Assume all paths exist

duckdb_build="${BUILD_DIR}/duckdb-0.3.3"
if [[ ! -d "${duckdb_build}" ]]; then
    duckdb_tarball="${DOWNLOAD_DIR}/duckdb.tar.gz"
    if [[ ! -f "${duckdb_tarball}" ]]; then
        wget https://github.com/duckdb/duckdb/archive/refs/tags/v0.3.3.tar.gz -O "${duckdb_tarball}"
    fi

    tar -xf "${duckdb_tarball}" -C "${BUILD_DIR}"
    patch -p1 -d "${duckdb_build}" < "${SCRIPT_PATH}/duckdb.patch"
fi

cd "${duckdb_build}"
mkdir -p build
cd build
if [[ ! -f Makefile ]]; then
    ${CMAKE} .. -DCMAKE_INSTALL_PREFIX="${duckdb_prefix}"
fi
make -j "${THREADS}"
# no need to install - sqlite3 wrapper is not installed
