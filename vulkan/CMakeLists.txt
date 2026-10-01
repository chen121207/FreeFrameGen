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

# Optional real-device smoke.  It loads vulkan-1.dll at runtime so machines
# without a Vulkan loader are reported as a CTest skip instead of failing the
# library build.  The test exercises the V3 timeline retirement contract and
# performs a small GPU readback; it is intentionally kept in the Vulkan-only
# project and is not part of the default D3D12 build.
include(CTest)
if(BUILD_TESTING)
  add_executable(ffg_vk_v3_smoke ../tests/ffg_vk_v3_smoke.cpp)
  target_include_directories(ffg_vk_v3_smoke PRIVATE ../include "${FFG_VK_HEADERS}")
  target_compile_definitions(ffg_vk_v3_smoke PRIVATE VK_NO_PROTOTYPES)
  target_compile_features(ffg_vk_v3_smoke PRIVATE cxx_std_17)
  target_compile_options(ffg_vk_v3_smoke PRIVATE /W4 /WX /wd4324 /utf-8)
  target_link_libraries(ffg_vk_v3_smoke PRIVATE FreeFrameGenVulkan)
  add_test(NAME vulkan_v3_smoke COMMAND ffg_vk_v3_smoke)
  add_executable(vulkan_external_protocol ../tests/vulkan_external_protocol.cpp)
  target_include_directories(vulkan_external_protocol PRIVATE ../include "${FFG_VK_HEADERS}")
  target_compile_definitions(vulkan_external_protocol PRIVATE VK_NO_PROTOTYPES)
  target_compile_features(vulkan_external_protocol PRIVATE cxx_std_17)
  target_compile_options(vulkan_external_protocol PRIVATE /W4 /WX /wd4324 /utf-8)
  target_link_libraries(vulkan_external_protocol PRIVATE FreeFrameGenVulkan)
  add_test(NAME vulkan_external_protocol COMMAND vulkan_external_protocol)
endif()
install(TARGETS FreeFrameGenVulkan RUNTIME DESTINATION bin ARCHIVE DESTINATION lib)
install(DIRECTORY ../include/ DESTINATION include)
