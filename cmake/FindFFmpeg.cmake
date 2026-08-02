#[[
Locates FFmpeg libraries and exposes them as imported targets:

  FFmpeg::avformat
  FFmpeg::avcodec
  FFmpeg::avfilter
  FFmpeg::avutil
  FFmpeg::swscale
  FFmpeg::swresample
  FFmpeg::FFmpeg   (all of the above combined)

Sets FFmpeg_FOUND when all required components are present.

pkg-config is used when it can answer, because it is the only source that also carries the
transitive link flags a static FFmpeg needs. Where it cannot — MSVC, and the prebuilt
win64/linux64 archives from BtbN/FFmpeg-Builds whose bundled .pc files carry the prefix of the
machine they were built on — the component falls back to a plain find_path/find_library pair,
which picks the archive up from CMAKE_PREFIX_PATH.
#]]

find_package(PkgConfig QUIET)

set(_ffmpeg_components avformat avcodec avfilter avutil swscale swresample)
set(_ffmpeg_all_found TRUE)

foreach(_comp ${_ffmpeg_components})
    string(TOUPPER ${_comp} _comp_upper)

    if(PkgConfig_FOUND)
        pkg_check_modules(PC_${_comp_upper} QUIET lib${_comp})
    endif()

    if(PC_${_comp_upper}_FOUND)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} INTERFACE IMPORTED)
            target_include_directories(FFmpeg::${_comp} INTERFACE ${PC_${_comp_upper}_INCLUDE_DIRS})
            target_link_libraries(FFmpeg::${_comp} INTERFACE ${PC_${_comp_upper}_LINK_LIBRARIES})
            target_compile_options(FFmpeg::${_comp} INTERFACE ${PC_${_comp_upper}_CFLAGS_OTHER})
        endif()
        continue()
    endif()

    find_path(FFMPEG_${_comp_upper}_INCLUDE_DIR
        NAMES lib${_comp}/${_comp}.h
        HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
        PATH_SUFFIXES include)
    find_library(FFMPEG_${_comp_upper}_LIBRARY
        NAMES ${_comp}
        HINTS ${FFMPEG_ROOT} $ENV{FFMPEG_ROOT}
        PATH_SUFFIXES lib)
    mark_as_advanced(FFMPEG_${_comp_upper}_INCLUDE_DIR FFMPEG_${_comp_upper}_LIBRARY)

    if(FFMPEG_${_comp_upper}_INCLUDE_DIR AND FFMPEG_${_comp_upper}_LIBRARY)
        if(NOT TARGET FFmpeg::${_comp})
            add_library(FFmpeg::${_comp} UNKNOWN IMPORTED)
            set_target_properties(FFmpeg::${_comp} PROPERTIES
                IMPORTED_LOCATION "${FFMPEG_${_comp_upper}_LIBRARY}"
                INTERFACE_INCLUDE_DIRECTORIES "${FFMPEG_${_comp_upper}_INCLUDE_DIR}")
        endif()
    else()
        set(_ffmpeg_all_found FALSE)
        message(WARNING "FFmpeg component lib${_comp} not found via pkg-config or CMAKE_PREFIX_PATH")
    endif()
endforeach()

if(_ffmpeg_all_found AND NOT TARGET FFmpeg::FFmpeg)
    add_library(FFmpeg::FFmpeg INTERFACE IMPORTED)
    target_link_libraries(FFmpeg::FFmpeg INTERFACE
        FFmpeg::avformat
        FFmpeg::avcodec
        FFmpeg::avfilter
        FFmpeg::avutil
        FFmpeg::swscale
        FFmpeg::swresample
    )
endif()

set(FFmpeg_FOUND ${_ffmpeg_all_found})

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(FFmpeg DEFAULT_MSG FFmpeg_FOUND)
