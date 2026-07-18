include(FindPackageHandleStandardArgs)

find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
  pkg_check_modules(PC_FFMPEG QUIET libavcodec libavformat libavutil libswscale)
endif()

find_path(
  FFmpeg_INCLUDE_DIR
  NAMES libavcodec/avcodec.h
  HINTS ${PC_FFMPEG_INCLUDE_DIRS}
)

set(_ffmpeg_required_vars)
foreach(component IN LISTS FFmpeg_FIND_COMPONENTS)
  find_library(
    FFmpeg_${component}_LIBRARY
    NAMES ${component} lib${component}
    HINTS ${PC_FFMPEG_LIBRARY_DIRS}
  )
  list(APPEND _ffmpeg_required_vars FFmpeg_${component}_LIBRARY)
endforeach()

find_package_handle_standard_args(
  FFmpeg
  REQUIRED_VARS FFmpeg_INCLUDE_DIR ${_ffmpeg_required_vars}
)

if(FFmpeg_FOUND)
  foreach(component IN LISTS FFmpeg_FIND_COMPONENTS)
    set(FFmpeg_${component}_FOUND TRUE)
    if(NOT TARGET FFmpeg::${component})
      add_library(FFmpeg::${component} INTERFACE IMPORTED)
      set_target_properties(
        FFmpeg::${component}
        PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${FFmpeg_INCLUDE_DIR}"
                   INTERFACE_LINK_LIBRARIES "${FFmpeg_${component}_LIBRARY}"
      )
    endif()
  endforeach()
endif()

mark_as_advanced(FFmpeg_INCLUDE_DIR ${_ffmpeg_required_vars})
