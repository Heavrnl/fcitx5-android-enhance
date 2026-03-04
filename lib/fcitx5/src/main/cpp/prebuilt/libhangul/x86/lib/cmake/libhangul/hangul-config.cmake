# libhangul
# Copyright 2021 Choe Hwanjin
#
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or (at your option) any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

include("${CMAKE_CURRENT_LIST_DIR}/hangul-config-version.cmake")


####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was hangul-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################

message("LIBHANGUL_INCLUDE_DIR: ${PACKAGE_PREFIX_DIR}/include/hangul-1.0")
message("LIBHANGUL_LIBRARY_DIR: ${PACKAGE_PREFIX_DIR}/lib")

set_and_check(LIBHANGUL_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include/hangul-1.0")
set_and_check(LIBHANGUL_LIBRARY_DIR "${PACKAGE_PREFIX_DIR}/lib")

set(LIBHANGUL_INCLUDE_DIR2 "include/hangul-1.0")
set(LIBHANGUL_LIBRARY_DIR2 "lib")


add_library(hangul SHARED IMPORTED)
set_target_properties(hangul PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES "${LIBHANGUL_INCLUDE_DIR}"
    IMPORTED_LOCATION "${LIBHANGUL_LIBRARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}hangul${CMAKE_SHARED_LIBRARY_SUFFIX}"
)
