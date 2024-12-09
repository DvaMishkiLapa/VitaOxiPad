cmake_minimum_required(VERSION 3.15)

# Set up the Vita SDK toolchain
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

# Application metadata
project(VitaOxiPad)
include("${VITASDK}/share/vita.cmake" REQUIRED)
set(VITA_APP_NAME "VitaOxiPad")
set(VITA_TITLEID  "VOXIPAD01")
set(VITA_VERSION  "01.20")

# Vita's IP to send over FTP
if(DEFINED ENV{VITA_IP})
  set(VITA_IP $ENV{VITA_IP})
endif()

# Compiler options
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++17")
set(VITA_MKSFOEX_FLAGS "${VITA_MKSFOEX_FLAGS} -d PARENTAL_LEVEL=1")
set(VITA_MAKE_FSELF_FLAGS "${VITA_MAKE_FSELF_FLAGS} -a 0x2800000000000001")

# Build FlatBuffers
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/../cmake/")
include(BuildFlatBuffers)

add_subdirectory(module)

link_directories(
  ${CMAKE_CURRENT_BINARY_DIR}
)

# Include directories
include_directories(
  ./include
  ../include
  module/include
  ${CMAKE_CURRENT_BINARY_DIR}/module
  ${CMAKE_BINARY_DIR}/generated
)

link_directories(${CMAKE_CURRENT_BINARY_DIR}/module)

file(GLOB SRC_FLATBUFFERS
  ../common/*.fbs
)

build_flatbuffers("${SRC_FLATBUFFERS}"
 ""
 flatbuffers_target
 ${CMAKE_CURRENT_LIST_DIR}
 ${CMAKE_BINARY_DIR}/generated
 ""
 "")

include_directories(${CMAKE_BINARY_DIR}/generated)

# App Source files
add_executable(${VITA_APP_NAME}
  src/main.cpp
  src/net.cpp
  src/status.cpp
  src/ctrl.cpp
  src/thread_helper.cpp
  src/draw_helper.cpp
)

add_compile_definitions(FLATBUFFERS_TRACK_VERIFIER_BUFFER_SIZE)
target_compile_options(${VITA_APP_NAME} PRIVATE -Wall -Wextra -Wpedantic -Werror)

# Dependencies
add_dependencies(${VITA_APP_NAME} flatbuffers_target)
add_dependencies(${VITA_APP_NAME} kctrl_all)

# Libraries
target_link_libraries(${VITA_APP_NAME}
  stdc++
  vita2d
  kctrl_stub_weak
  taihen_stub
  SceCtrl_stub
  SceTouch_stub
  SceDisplay_stub
  SceMotion_stub
  SceAppMgr_stub
  ScePgf_stub
  ScePower_stub
  SceCommonDialog_stub
  SceGxm_stub
  SceVshBridge_stub
  SceShellSvc_stub
  SceLibDbg_stub
)

# Create the application self and VPK
vita_create_self(${VITA_APP_NAME}.self ${VITA_APP_NAME})
vita_create_vpk(${VITA_APP_NAME}.vpk ${VITA_TITLEID} ${VITA_APP_NAME}.self
  VERSION ${VITA_VERSION}
  NAME ${VITA_APP_NAME}
  FILE sce_sys/icon0.png sce_sys/icon0.png
  FILE sce_sys/pic0.png sce_sys/pic0.png
  FILE sce_sys/livearea/contents/bg.png sce_sys/livearea/contents/bg.png
  FILE sce_sys/livearea/contents/startup.png sce_sys/livearea/contents/startup.png
  FILE sce_sys/livearea/contents/template.xml sce_sys/livearea/contents/template.xml
  FILE ${CMAKE_CURRENT_BINARY_DIR}/module/kctrl.skprx module/kctrl.skprx
)

# Optional: Send the VPK directly to the PS Vita
if (VITA_IP)
  add_custom_target(send
    COMMAND "echo" destroy | nc ${VITA_IP} 1338
    COMMAND "sleep" 1s
    COMMAND unzip -o ${VITA_APP_NAME}.vpk eboot.bin -d ${CMAKE_BINARY_DIR}
    COMMAND "curl" --ftp-method nocwd -T ${CMAKE_BINARY_DIR}/eboot.bin "ftp://${VITA_IP}:1337/ux0:/app/${VITA_TITLEID}/"
    COMMAND "curl" --ftp-method nocwd -T ${CMAKE_BINARY_DIR}/module/kctrl.skprx "ftp://${VITA_IP}:1337/ux0:/app/${VITA_TITLEID}/module/"
    COMMAND "echo" launch ${VITA_TITLEID} | nc ${VITA_IP} 1338
    DEPENDS ${VITA_APP_NAME}.vpk
  )
endif()

if(DEFINED ENV{VITA_IP})
  set(PARSECOREPATH $ENV{PARSECOREPATH})
endif()

# Optional: Dump debugging information
add_custom_target(dump
  COMMAND ${CMAKE_SOURCE_DIR}/get_dump.sh ${VITA_IP}
  COMMAND python3 ${PARSECOREPATH} coredump ${CMAKE_PROJECT_NAME}
)
