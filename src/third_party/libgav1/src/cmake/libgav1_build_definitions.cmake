if(LIBGAV1_CMAKE_LIBGAV1_BUILD_DEFINITIONS_CMAKE_)
  return()
endif() # LIBGAV1_CMAKE_LIBGAV1_BUILD_DEFINITIONS_CMAKE_
set(LIBGAV1_CMAKE_LIBGAV1_BUILD_DEFINITIONS_CMAKE_ 1)

macro(libgav1_set_build_definitions)
  # TODO(tomfinegan): Neither LIBGAV1_VERSION nor LIBGAV1_SOVERSION should be
  # defined here. This data should come from outside the build system and be
  # loaded by the build system when needed.
  set(LIBGAV1_VERSION "0.0.0")
  set(LIBGAV1_SOVERSION 0)

  list(APPEND libgav1_include_paths "${libgav1_root}" "${libgav1_root}/src"
              "${libgav1_build}" "${libgav1_root}/third_party/abseil-cpp")
  list(APPEND libgav1_gtest_include_paths
              "third_party/googletest/googlemock/include"
              "third_party/googletest/googletest/include"
              "third_party/googletest/googletest")
  list(APPEND libgav1_test_include_paths ${libgav1_include_paths}
              ${libgav1_gtest_include_paths} ${libgav1_root})
  list(APPEND libgav1_defines "LIBGAV1_CMAKE=1"
              "LIBGAV1_FLAGS_SRCDIR=\"${libgav1_root}\""
              "LIBGAV1_FLAGS_TMPDIR=\"/tmp\"")

  if(MSVC OR WIN32)
    list(APPEND libgav1_defines "_CRT_SECURE_NO_DEPRECATE=1" "NOMINMAX=1")
  endif()

  if(ANDROID)
    if(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
      set(CMAKE_ANDROID_ARM_MODE ON)
    endif()
  endif()

  list(APPEND libgav1_base_cxx_flags "-Wall" "-Wextra" "-Wmissing-declarations"
              "-Wno-sign-compare" "-fvisibility=hidden"
              "-fvisibility-inlines-hidden")

  if(BUILD_SHARED_LIBS)
    set(CMAKE_POSITION_INDEPENDENT_CODE ON)
  endif()

  list(APPEND libgav1_clang_cxx_flags "-Wmissing-prototypes"
              "-Wshorten-64-to-32")

  if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    if(CMAKE_CXX_COMPILER_VERSION VERSION_GREATER_EQUAL "7")
      # Quiet warnings due to potential snprintf() truncation in threadpool.cc.
      list(APPEND libgav1_base_cxx_flags "-Wno-format-truncation")

      if(CMAKE_SYSTEM_PROCESSOR STREQUAL "armv7")
        # Quiet gcc 6 vs 7 abi warnings:
        # https://gcc.gnu.org/bugzilla/show_bug.cgi?id=77728
        list(APPEND libgav1_base_cxx_flags "-Wno-psabi")
        list(APPEND ABSL_GCC_FLAGS "-Wno-psabi")
      endif()
    endif()
  endif()

  if(CMAKE_BUILD_TYPE MATCHES "Rel|REL")
    # TODO(tomfinegan): this value is only a concern for the core library and
    # can be made smaller if the test targets are avoided.
    list(APPEND libgav1_base_cxx_flags "-Wstack-usage=196608")
  endif()

  list(APPEND libgav1_msvc_cxx_flags
              # Warning level 3.
              "/W3"
              # Disable warning C4018:
              # '<comparison operator>' signed/unsigned mismatch
              "/wd4018"
              # Disable warning C4244:
              # 'argument': conversion from '<double/int>' to
              # '<float/smaller int type>', possible loss of data
              "/wd4244"
              # Disable warning C4267:
              # '=': conversion from '<double/int>' to
              # '<float/smaller int type>', possible loss of data
              "/wd4267"
              # Disable warning C4309:
              # 'argument': truncation of constant value
              "/wd4309"
              # Disable warning C4551:
              # function call missing argument list
              "/wd4551")

  if(BUILD_SHARED_LIBS)
    list(APPEND libgav1_msvc_cxx_flags
                # Disable warning C4251:
                # 'libgav1::DecoderImpl class member' needs to have
                # dll-interface to be used by clients of class
                # 'libgav1::Decoder'.
                "/wd4251")
  endif()

  if(NOT LIBGAV1_MAX_BITDEPTH)
    set(LIBGAV1_MAX_BITDEPTH 10)
  elseif(NOT LIBGAV1_MAX_BITDEPTH EQUAL 8 AND NOT LIBGAV1_MAX_BITDEPTH EQUAL 10)
    libgav1_die("LIBGAV1_MAX_BITDEPTH must be 8 or 10.")
  endif()

  list(APPEND libgav1_defines "LIBGAV1_MAX_BITDEPTH=${LIBGAV1_MAX_BITDEPTH}")

  # Source file names ending in these suffixes will have the appropriate
  # compiler flags added to their compile commands to enable intrinsics.
  set(libgav1_neon_source_file_suffix "neon.cc")
  set(libgav1_sse4_source_file_suffix "sse4.cc")
endmacro()
