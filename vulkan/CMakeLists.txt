cmake_minimum_required(VERSION 3.24)
project(FreeFrameGenVulkan LANGUAGES C CXX)
if(NOT WIN32)
  message(FATAL_ERROR "Windows only")
endif()
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
set(FFG_VULKAN_INCLUDE_DIR "" CACHE PATH "Vulkan-Headers include directory")
find_path(FFG_VK_HEADERS vulkan/vulkan.h HINTS "${FFG_VULKAN_INCLUDE_DIR}" "$ENV{VULKAN_SDK}/Include" REQUIRED)
find_program(FFG_GLSLANG glslangValidator HINTS "$ENV{VULKAN_SDK}/Bin" REQUIRED)
add_custom_command(OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/ffg_interpolate.h"
  COMMAND "${FFG_GLSLANG}" -V --target-env vulkan1.2 --vn ffg_interpolate
    -o "${CMAKE_CURRENT_BINARY_DIR}/ffg_interpolate.h"
    "${CMAKE_CURRENT_LIST_DIR}/../shaders/interpolate.comp"
  DEPENDS ../shaders/interpolate.comp VERBATIM)
add_library(FreeFrameGenVulkan SHARED ../src/ffg_vk.cpp "${CMAKE_CURRENT_BINARY_DIR}/ffg_interpolate.h")
target_include_directories(FreeFrameGenVulkan PUBLIC ../include "${FFG_VK_HEADERS}"
  PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")
target_compile_definitions(FreeFrameGenVulkan PRIVATE FFG_VK_BUILD VK_NO_PROTOTYPES)
target_compile_features(FreeFrameGenVulkan PRIVATE cxx_std_17)
target_compile_options(FreeFrameGenVulkan PRIVATE /W4 /WX /wd4324 /utf-8)
install(TARGETS FreeFrameGenVulkan RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(DIRECTORY ../include/ DESTINATION include)
