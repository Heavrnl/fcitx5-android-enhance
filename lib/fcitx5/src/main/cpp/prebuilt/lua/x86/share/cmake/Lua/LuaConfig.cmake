
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was LuaConfig.cmake.in                            ########

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

include("${CMAKE_CURRENT_LIST_DIR}/LuaTargets.cmake")

set_and_check(LUA_INCLUDE_DIR "${PACKAGE_PREFIX_DIR}/include")
add_library(Lua::Library ALIAS "Lua::lua_static")
get_target_property(LUA_CONFIG "Lua::lua_static" IMPORTED_CONFIGURATIONS)
get_target_property(LUA_LIBRARY "Lua::lua_static" "IMPORTED_LOCATION_${LUA_CONFIG}")
set(LUA_LIBRARIES "${LUA_LIBRARY}")
add_library(LUA_INCLUDE_LIB INTERFACE)
target_include_directories(LUA_INCLUDE_LIB INTERFACE "${LUA_INCLUDE_DIR}")
list(APPEND LUA_LIBRARIES LUA_INCLUDE_LIB)

foreach(LIB_NAME m;dl)
    find_library(LIB_LOCATION "${LIB_NAME}")
    if(NOT LIB_LOCATION)
        message(FATAL_ERROR "lib${LIB_NAME} not found and is required by lua")
    else()
        list(APPEND LUA_LIBRARIES "${LIB_LOCATION}")
    endif()
endforeach()

check_required_components(Lua)
