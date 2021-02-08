#!/usr/bin/env bash
# This file is part of GUFI, which is part of MarFS, which is released
# under the BSD license.
#
#
# Copyright (c) 2017, Los Alamos National Security (LANS), LLC
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without modification,
# are permitted provided that the following conditions are met:
#
# 1. Redistributions of source code must retain the above copyright notice, this
# list of conditions and the following disclaimer.
#
# 2. Redistributions in binary form must reproduce the above copyright notice,
# this list of conditions and the following disclaimer in the documentation and/or
# other materials provided with the distribution.
#
# 3. Neither the name of the copyright holder nor the names of its contributors
# may be used to endorse or promote products derived from this software without
# specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
# ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
# WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
# IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT,
# INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING,
# BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
# OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
# ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
#
#
# From Los Alamos National Security, LLC:
# LA-CC-15-039
#
# Copyright (c) 2017, Los Alamos National Security, LLC All rights reserved.
# Copyright 2017. Los Alamos National Security, LLC. This software was produced
# under U.S. Government contract DE-AC52-06NA25396 for Los Alamos National
# Laboratory (LANL), which is operated by Los Alamos National Security, LLC for
# the U.S. Department of Energy. The U.S. Government has rights to use,
# reproduce, and distribute this software.  NEITHER THE GOVERNMENT NOR LOS
# ALAMOS NATIONAL SECURITY, LLC MAKES ANY WARRANTY, EXPRESS OR IMPLIED, OR
# ASSUMES ANY LIABILITY FOR THE USE OF THIS SOFTWARE.  If software is
# modified to produce derivative works, such modified software should be
# clearly marked, so as not to confuse it with the version available from
# LANL.
#
# THIS SOFTWARE IS PROVIDED BY LOS ALAMOS NATIONAL SECURITY, LLC AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
# THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL LOS ALAMOS NATIONAL SECURITY, LLC OR
# CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
# EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
# OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
# INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
# CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
# IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
# OF SUCH DAMAGE.



set -e

ROOT="@CMAKE_BINARY_DIR@"
GUFI_PREFIX="${ROOT}/src"

if [[ "$#" -lt 3 ]]; then
    echo "Syntax: $0 GUFI_index thread_count output_prefix"
    exit 1
fi

index="$1"
threads="$2"
out="$3"

outtable="stats"

rm -rf ${out}.*

# walk the index once
${GUFI_PREFIX}/gufi_query -n "${threads}" -O "${out}" \
    -I "CREATE TABLE ${outtable}(path TEXT, subdirs INTEGER, depth INTEGER, size INTEGER, count INTEGER)" \
    -E "INSERT INTO ${outtable} SELECT path(), (SELECT nlink - 2 FROM summary), level(), TOTAL(size), COUNT(*) FROM entries" -d " " "${index}"

QUERYDBN="${GUFI_PREFIX}/querydbn"
table="${outtable}"  # becomes v${table}

# total
total=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT COUNT(*) FROM v${table}" ${out}.* | head -n 1 | sed 's/|$//g')

# empty count
empty=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT COUNT(*) FROM v${table} WHERE count == 0" ${out}.* | head -n 1 | sed 's/|$//g')

# non-empty count
nonempty=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT COUNT(*) FROM v${table} WHERE count > 0" ${out}.* | head -n 1 | sed 's/|$//g')

# file count (non-empty dirs) (no need for "WHERE count > 0" because it just adds 0s)
files=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT SUM(count) FROM v${table}" ${out}.* | head -n 1 | sed 's/|$//g')

# total size (non-empty dirs) (no need for "WHERE count > 0" because it just adds 0s)
size=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT SUM(size) FROM v${table}" ${out}.* | head -n 1 | sed 's/|$//g')

# total depth
depth=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT SUM(depth) FROM v${table}" ${out}.* | head -n 1 | sed 's/|$//g')

# non-leaf directory count
nonleaf=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT COUNT(*) FROM v${table} WHERE subdirs > 0" ${out}.* | head -n 1 | sed 's/|$//g')

# total branching factor (non-leaf dirs)
bf=$(${QUERYDBN} -V "${outtable}" "${table}" "SELECT SUM(subdirs) FROM v${table} WHERE subdirs > 0" ${out}.* | head -n 1 | sed 's/|$//g')

echo "Total Dirs:                             ${total}"
echo "Empty Dirs:                             ${empty} ($( (echo -n "scale=4;"; echo ${empty} \* 100.0 / ${total}) | bc -l)%)"
echo "Non-Empty Dirs:                         ${nonempty} ($( (echo -n "scale=4;"; echo ${nonempty} \* 100.0 / ${total}) | bc -l)%)"
echo "Avg No. Files of Non-empty Dirs:        ${files} / ${nonempty} = $( (echo -n "scale=4;"; echo ${files} / ${nonempty}) | bc -l)"
echo "Avg Size of Non-empty Dirs:             ${size} / ${nonempty} = $( (echo -n "scale=4;"; echo ${size} / ${nonempty}) | bc -l) bytes"
echo "Avg Depth:                              ${depth} / ${total} = $( (echo -n "scale=4;"; echo ${depth} / ${total}) | bc -l)"
echo "Avg Branching Factor of Non-leaf Dirs:  ${bf} / ${nonleaf} = $( (echo -n "scale=4;"; echo ${bf} / ${nonleaf}) | bc -l)"
