cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" ON)
option(ENABLE_QT "Use Qt functionality" ON)

if(NOT ENABLE_FRONTEND_API OR NOT ENABLE_QT)
  message(FATAL_ERROR "OBS Timelapse requires obs-frontend-api and Qt 6.")
endif()

include(compilerconfig)
include(defaults)
include(helpers)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

find_package(FFmpeg REQUIRED COMPONENTS avcodec avformat avutil swscale)
target_link_libraries(
  ${CMAKE_PROJECT_NAME}
  PRIVATE FFmpeg::avcodec FFmpeg::avformat FFmpeg::avutil FFmpeg::swscale
)

find_package(obs-frontend-api REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)

find_package(Qt6 REQUIRED COMPONENTS Widgets Core)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Widgets)
target_compile_options(
  ${CMAKE_PROJECT_NAME}
  PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
)

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/capture-session.cpp
          src/controls-button.cpp
          src/controller.cpp
          src/core-utils.cpp
          src/mkv-writer.cpp
          src/png-writer.cpp
          src/plugin-main.cpp
          src/settings.cpp
          src/timelapse-dialog.cpp
)

include(CTest)
if(BUILD_TESTING)
  add_executable(
    obs-timelapse-core-tests
    tests/test-core-utils.cpp
    src/core-utils.cpp
    src/mkv-writer.cpp
    src/png-writer.cpp
    src/settings.cpp
  )
  target_include_directories(obs-timelapse-core-tests PRIVATE src)
  target_link_libraries(
    obs-timelapse-core-tests
    PRIVATE OBS::libobs
            OBS::obs-frontend-api
            Qt6::Core
            Qt6::Gui
            FFmpeg::avcodec
            FFmpeg::avformat
            FFmpeg::avutil
            FFmpeg::swscale
  )
  add_test(NAME obs-timelapse-core-tests COMMAND obs-timelapse-core-tests)
endif()

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})
