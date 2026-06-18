cmake_minimum_required(VERSION 3.25)
project(fische LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} ${PROJECT_SOURCE_DIR})

find_package(OpenGL REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(glm CONFIG QUIET)

if(NOT glm_FOUND)
  find_path(GLM_INCLUDE_DIR glm/glm.hpp
    PATHS
      "$ENV{VCPKG_ROOT}/installed/${VCPKG_TARGET_TRIPLET}/include"
      "C:/vcpkg/installed/${VCPKG_TARGET_TRIPLET}/include"
      "C:/vcpkg/installed/x64-windows-static/include"
      "C:/vcpkg/installed/x86-windows-static/include"
    REQUIRED)
endif()

set(RESOURCE_FILE "${CMAKE_CURRENT_SOURCE_DIR}/src/fische.rc")
set_source_files_properties(${RESOURCE_FILE} PROPERTIES
    COMPILE_FLAGS "/nologo"
)

set(ASIO_DIR ${PROJECT_SOURCE_DIR}/src/asio)

set_source_files_properties(
  src/asio/common/asio.cpp
  src/asio/host/asiodrivers.cpp
  src/asio/host/pc/asiolist.cpp
  PROPERTIES
  COMPILE_DEFINITIONS "_CRT_SECURE_NO_WARNINGS"
  COMPILE_FLAGS "/FI\"objbase.h\""
)

add_executable(fische
  src/analyst.cpp
  src/audiobuffer.cpp
  src/blurengine.cpp
  src/cpudetect.cpp
  src/fische.cpp
  src/screenbuffer.cpp
  src/vector.cpp
  src/vectorfield.cpp
  src/wavepainter.cpp
  src/fishe_xbmc.cpp
  src/glad/src/glad.c
  src/main.cpp
  src/app_settings.cpp
  src/settings_dialog.cpp
  src/wasapi_capture.cpp
  src/spout/Spout.cpp
  src/spout/SpoutDirectX.cpp
  src/spout/SpoutGL.cpp
  src/spout/SpoutFrameCount.cpp
  src/spout/SpoutSender.cpp
  src/spout/SpoutUtils.cpp
  src/spout/SpoutGLextensions.cpp
  src/spout/SpoutSenderNames.cpp
  src/spout/SpoutSharedMemory.cpp
  src/spout/SpoutReceiver.cpp
  src/spout/SpoutCopy.cpp
  src/asio_capture.h
  src/asio_capture.cpp
  ${ASIO_DIR}/common/asio.cpp
  ${ASIO_DIR}/host/asiodrivers.cpp
  ${ASIO_DIR}/host/pc/asiolist.cpp
  ${RESOURCE_FILE})

target_include_directories(fische PRIVATE
  ${PROJECT_SOURCE_DIR}/src
  ${PROJECT_SOURCE_DIR}/src/glad/include
  ${ASIO_DIR}/common
  ${ASIO_DIR}/host
  ${ASIO_DIR}/host/pc)

if(glm_FOUND)
  target_link_libraries(fische PRIVATE glm::glm)
else()
  target_include_directories(fische PRIVATE ${GLM_INCLUDE_DIR})
endif()

target_compile_definitions(fische PRIVATE
  HAS_GL
  GLFW_EXPOSE_NATIVE_WIN32
  NOMINMAX
  WIN32_LEAN_AND_MEAN)

if(MSVC)
  target_compile_options(fische PRIVATE /W3 /permissive-)
  target_link_options(fische PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

target_link_libraries(fische PRIVATE
  glfw
  OpenGL::GL
  version
  d3d11
  dxgi
  d3dcompiler
  winmm
  wininet
  user32
  gdi32
  comctl32
  ole32
  uuid
  propsys
  avrt)

