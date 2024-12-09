cmake_minimum_required(VERSION 3.15)

# Set up the Vita SDK toolchain
if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  if(DEFINED ENV{VITASDK})
    set(CMAKE_TOOLCHAIN_FILE "$ENV{VITASDK}/share/vita.toolchain.cmake" CACHE PATH "Toolchain file")
  else()
    message(FATAL_ERROR "Please define VITASDK to point to your SDK path!")
  endif()
endif()

project(kctrl)
include("${VITASDK}/share/vita.cmake" REQUIRED)

set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wl,-q -Wall -O3 -nostdlib")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti -fno-exceptions")

# Add the module's source files
add_executable(${PROJECT_NAME}
  src/main.c
)

# Link against necessary kernel stubs
target_link_libraries(${PROJECT_NAME}
  SceDebugForDriver_stub
  SceCtrlForDriver_stub
  SceModulemgrForDriver_stub
  SceSysmemForDriver_stub
  SceOledForDriver_stub_weak
  SceLcdForDriver_stub_weak
)

# Include the module's include directory
target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Set linker flags
set_target_properties(${PROJECT_NAME}
  PROPERTIES LINK_FLAGS "-nostdlib"
)

# Create the kernel module (skprx)
vita_create_self(${PROJECT_NAME}.skprx ${PROJECT_NAME}
  CONFIG kctrl.yml
  UNSAFE
)

# Generate stubs for the module
vita_create_stubs(stubs ${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/kctrl.yml KERNEL)
# vita_create_stubs(stubs ${PROJECT_NAME} ${CMAKE_CURRENT_SOURCE_DIR}/kctrl.yml USER)


# Ensure that building the module also builds the stubs
add_custom_target(kctrl_all
  ALL
  DEPENDS ${PROJECT_NAME}.skprx-self
  DEPENDS lib${PROJECT_NAME}_stub.a
  DEPENDS lib${PROJECT_NAME}_stub_weak.a
)
