if(LIBGAV1_CMAKE_LIBGAV1_OPTIONS_CMAKE_)
  return()
endif() # LIBGAV1_CMAKE_LIBGAV1_OPTIONS_CMAKE_
set(LIBGAV1_CMAKE_LIBGAV1_OPTIONS_CMAKE_)

# Simple wrapper for CMake's builtin option command that tracks libgav1's build
# options in the list variable $libgav1_options.
macro(libgav1_option)
  unset(option_NAME)
  unset(option_HELPSTRING)
  unset(option_VALUE)
  unset(optional_args)
  unset(multi_value_args)
  set(single_value_args NAME HELPSTRING VALUE)
  cmake_parse_arguments(option "${optional_args}" "${single_value_args}"
                        "${multi_value_args}" ${ARGN})

  if(NOT (option_NAME AND option_HELPSTRING AND DEFINED option_VALUE))
    message(FATAL_ERROR "libgav1_option: NAME HELPSTRING and VALUE required.")
  endif()

  option(${option_NAME} ${option_HELPSTRING} ${option_VALUE})

  if(LIBGAV1_VERBOSE GREATER 2)
    message("--------- libgav1_option ---------\n"
            "option_NAME=${option_NAME}\n"
            "option_HELPSTRING=${option_HELPSTRING}\n"
            "option_VALUE=${option_VALUE}\n"
            "------------------------------------------\n")
  endif()

  list(APPEND libgav1_options ${option_NAME})
  list(REMOVE_DUPLICATES libgav1_options)
endmacro()

# Dumps the $libgav1_options list via CMake message command.
macro(libgav1_dump_options)
  foreach(option_name ${libgav1_options})
    message("${option_name}: ${${option_name}}")
  endforeach()
endmacro()
