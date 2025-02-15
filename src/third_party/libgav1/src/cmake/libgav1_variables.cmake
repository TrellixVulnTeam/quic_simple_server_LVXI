if(LIBGAV1_CMAKE_LIBGAV1_VARIABLES_CMAKE_)
  return()
endif() # LIBGAV1_CMAKE_LIBGAV1_VARIABLES_CMAKE_
set(LIBGAV1_CMAKE_LIBGAV1_VARIABLES_CMAKE_ 1)

# Halts generation when $variable_name does not refer to a directory that
# exists.
macro(libgav1_variable_must_be_directory variable_name)
  if("${variable_name}" STREQUAL "")
    message(
      FATAL_ERROR
        "Empty variable_name passed to libgav1_variable_must_be_directory.")
  endif()

  if("${${variable_name}}" STREQUAL "")
    message(
      FATAL_ERROR
        "Empty variable ${variable_name} is required to build libgav1.")
  endif()

  if(NOT IS_DIRECTORY "${${variable_name}}")
    message(
      FATAL_ERROR
        "${variable_name}, which is ${${variable_name}}, does not refer to a\n"
        "directory.")
  endif()
endmacro()

# Adds $var_name to the tracked variables list.
macro(libgav1_track_configuration_variable var_name)
  if(LIBGAV1_VERBOSE GREATER 2)
    message("---- libgav1_track_configuration_variable ----\n"
            "var_name=${var_name}\n"
            "----------------------------------------------\n")
  endif()

  list(APPEND libgav1_configuration_variables ${var_name})
  list(REMOVE_DUPLICATES libgav1_configuration_variables)
endmacro()

# Logs current C++ and executable linker flags via CMake's message command.
macro(libgav1_dump_cmake_flag_variables)
  unset(flag_variables)
  list(APPEND flag_variables "CMAKE_CXX_FLAGS_INIT" "CMAKE_CXX_FLAGS"
              "CMAKE_EXE_LINKER_FLAGS_INIT" "CMAKE_EXE_LINKER_FLAGS")
  if(CMAKE_BUILD_TYPE)
    list(APPEND flag_variables "CMAKE_BUILD_TYPE"
                "CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}_INIT"
                "CMAKE_CXX_FLAGS_${CMAKE_BUILD_TYPE}"
                "CMAKE_EXE_LINKER_FLAGS_${CMAKE_BUILD_TYPE}_INIT"
                "CMAKE_EXE_LINKER_FLAGS_${CMAKE_BUILD_TYPE}")
  endif()
  foreach(flag_variable ${flag_variables})
    message("${flag_variable}:${${flag_variable}}")
  endforeach()
endmacro()

# Dumps the variables tracked in $libgav1_configuration_variables via CMake's
# message command.
macro(libgav1_dump_tracked_configuration_variables)
  foreach(config_variable ${libgav1_configuration_variables})
    message("${config_variable}:${${config_variable}}")
  endforeach()
endmacro()
