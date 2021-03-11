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



cmake_minimum_required(VERSION 3.0.0)

set(PDFLATEX_COMPILER pdflatex)
include(UseLATEX)

add_custom_target(tex_files)

set(TEX_FILES)

# add non-generated tex files
function(add_latex_files TARGET)
  foreach(TEX ${ARGN})
    set(SOURCE "${CMAKE_CURRENT_SOURCE_DIR}/${TEX}")
    set(TARGET "${CMAKE_CURRENT_BINARY_DIR}/${TEX}")
    get_filename_component(NAME "${SOURCE}" NAME)

    # copy the file to the build directory
    add_custom_command(OUTPUT "${TARGET}"
      COMMAND "${CMAKE_COMMAND}" -E copy "${SOURCE}" "${TARGET}"
      DEPENDS "${SOURCE}"
      COMMENT "Generating ${NAME}"
    )

    # create a target for the file
    add_custom_target("${TEX}"
      DEPENDS "${TARGET}"
    )

    add_dependencies(tex_files "${TEX}")

    list(APPEND TEX_FILES "${TARGET}" PARENT_SCOPE)
  endforeach()
endfunction()

# main is root dependency
# contains a bunch of \includes and \inputs
configure_file(main.tex main.tex @ONLY)

# add non-generated files
add_latex_files(tex_files intro.tex)

# generate license section
get_filename_component(LICENSE_SOURCE "${CMAKE_SOURCE_DIR}/LICENSE.txt" REALPATH)
set(LICENSE_TEX "${CMAKE_CURRENT_BINARY_DIR}/license.tex")
add_custom_command(OUTPUT "${LICENSE_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\section{License}" > "${LICENSE_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\label{sec:license}" >> "${LICENSE_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\texttt{\\\\input{${LICENSE_SOURCE}}}" >> "${LICENSE_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\newpage" >> "${LICENSE_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo >> "${LICENSE_TEX}"
  COMMENT "Generating license.tex"
  DEPENDS "${LICENSE_SOURCE}"
)

add_custom_target(LICENSE_TEX
  DEPENDS "${LICENSE_TEX}"
)

add_dependencies(tex_files LICENSE_TEX)

# generate file containing command references
set(COMMANDS_TEX "${CMAKE_CURRENT_BINARY_DIR}/commands.tex")
add_custom_command(OUTPUT "${COMMANDS_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\section{Command Reference}" > "${COMMANDS_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo "\\\\label{sec:commands}" >> "${COMMANDS_TEX}"
  COMMAND "${CMAKE_COMMAND}" -E echo >> "${COMMANDS_TEX}"
  COMMENT "Generating commands.tex"
)

add_custom_target(COMMANDS_TEX
  DEPENDS "${COMMANDS_TEX}"
)

add_dependencies(tex_files COMMANDS_TEX)

# generate help output, save it to a file, and include it in commands.tex
# must not be run in parallel
function (generate_help FLAG)
  foreach(EXEC ${ARGN})
    get_filename_component(REALPATH "${CMAKE_BINARY_DIR}/${EXEC}" REALPATH)
    get_filename_component(REALDIR "${REALPATH}" DIRECTORY)
    get_filename_component(NAME "${REALPATH}" NAME)
    set(HELP_FILE "${CMAKE_CURRENT_BINARY_DIR}/${NAME}.help")
    set(TEX_FILE "${CMAKE_CURRENT_BINARY_DIR}/${NAME}.tex")
    set(TARGET "${NAME}_help")

    string(REPLACE "_" "\\\\_" LATEX_NAME "${NAME}")

    # save -h output
    add_custom_command(OUTPUT "${HELP_FILE}"
      COMMAND "${REALPATH}" "${FLAG}" > "${HELP_FILE}" 2>&1 || true
      COMMAND sed -i "s+${REALDIR}/++g" "${HELP_FILE}"
      COMMAND "${CMAKE_COMMAND}" -E echo "\\\\subsection{${LATEX_NAME}}" >> "${COMMANDS_TEX}"
      COMMAND "${CMAKE_COMMAND}" -E echo "\\\\label{cmd:${NAME}}" >> "${COMMANDS_TEX}"
      COMMAND "${CMAKE_COMMAND}" -E echo "\\\\lstinputlisting[basicstyle=\\\\ttfamily,breaklines=true]{${HELP_FILE}}" >> "${COMMANDS_TEX}"
      COMMAND "${CMAKE_COMMAND}" -E echo >> "${COMMANDS_TEX}"
      WORKING_DIRECTORY "${REALDIR}"
      DEPENDS "${REALPATH}"
    )

    add_custom_target("${TARGET}"
      DEPENDS "${HELP_FILE}" COMMANDS_TEX
    )

    add_dependencies(tex_files "${TARGET}")
  endforeach()
endfunction()

# not sure how to keep the executable lists in sync with src/scripts
# generate help output with -h
generate_help(-h
  "src/gufi_dir2index"
  "src/gufi_dir2trace"
  "src/gufi_trace2index"
  "src/gufi_query"
  "src/querydb"
  "src/querydbn"
  "scripts/gufi_find"
  "scripts/gufi_stats"
)

# generate help output with --help
generate_help(--help
  "scripts/gufi_ls")

add_latex_document(main.tex
  INPUTS ${TEX_FILES}
  DEPENDS tex_files
  FORCE_PDF
  TARGET_NAME docs
  EXCLUDE_FROM_ALL
)
