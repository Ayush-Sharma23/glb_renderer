cmake_minimum_required(VERSION 3.20)
project(glb_renderer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler warnings
if(MSVC)
    add_compile_options(/W4 /WX)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Werror -Wno-unused-parameter -Wno-missing-field-initializers)
endif()

# Find Vulkan + GLFW
find_package(Vulkan REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)

# Fetch dependencies
include(FetchContent)

# cgltf - single header glTF loader
FetchContent_Declare(
    cgltf
    GIT_REPOSITORY https://github.com/jkuhlmann/cgltf
    GIT_TAG v1.13
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(cgltf)

# GLM - math library
FetchContent_Declare(
    glm
    GIT_REPOSITORY https://github.com/g-truc/glm
    GIT_TAG 0.9.9.8
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(glm)

# vk-bootstrap - Vulkan bootstrap library
FetchContent_Declare(
    vk_bootstrap
    GIT_REPOSITORY https://github.com/charles-lunarg/vk-bootstrap
    GIT_TAG v1.4.350
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vk_bootstrap)

# VMA - Vulkan Memory Allocator
FetchContent_Declare(
    vma
    GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
    GIT_TAG v3.1.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(vma)

# stb_image - image loading
FetchContent_Declare(
    stb
    GIT_REPOSITORY https://github.com/nothings/stb
    GIT_TAG master
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(stb)

# Include directories
include_directories(
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${cgltf_SOURCE_DIR}
    ${glm_SOURCE_DIR}
    ${vk_bootstrap_SOURCE_DIR}/src
    ${vma_SOURCE_DIR}/include
    ${stb_SOURCE_DIR}
    ${GLFW_INCLUDE_DIRS}
)

# Shader compilation
add_custom_target(shaders ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_CURRENT_BINARY_DIR}/shaders
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.vert -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/vertex.vert.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.frag -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/fragment.frag.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.vert -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/pbr.vert.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.frag -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/pbr.frag.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.vert -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox.vert.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.frag -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/skybox.frag.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/fullscreen.vert.spv
    COMMAND glslc ${CMAKE_CURRENT_SOURCE_DIR}/shaders/brdf.frag -o ${CMAKE_CURRENT_BINARY_DIR}/shaders/brdf.frag.spv
    DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/shaders/vertex.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fragment.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/pbr.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/skybox.frag
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/fullscreen.vert
            ${CMAKE_CURRENT_SOURCE_DIR}/shaders/brdf.frag
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Compiling shaders"
)

# Main executable
add_executable(glb_renderer
    src/main.cpp
    src/renderer.cpp
    src/renderer.hpp
    src/vulkan_context.cpp
    src/vulkan_context.hpp
    src/vma_impl.cpp
    src/stb_impl.cpp
    src/gltf_loader.cpp
    src/gltf_loader.hpp
    src/scene.cpp
    src/scene.hpp
    src/buffer.cpp
    src/buffer.hpp
    src/mesh.cpp
    src/mesh.hpp
    src/pipeline.cpp
    src/pipeline.hpp
    src/camera.cpp
    src/camera.hpp
    src/image.cpp
    src/image.hpp
    src/sampler.cpp
    src/sampler.hpp
    src/texture.cpp
    src/texture.hpp
    src/material.hpp
    src/descriptor_set.cpp
    src/descriptor_set.hpp
    src/shader_path.hpp
    src/frustum.cpp
    src/frustum.hpp
    src/indirect_draw.cpp
    src/indirect_draw.hpp
    src/skin.cpp
    src/skin.hpp
    src/profiler.hpp
    src/profiler.cpp
    src/ibl.cpp
    src/ibl.hpp
)

target_link_libraries(glb_renderer PRIVATE
    Vulkan::Vulkan
    vk-bootstrap
    ${GLFW_LIBRARIES}
    pthread
)

target_compile_definitions(glb_renderer PRIVATE
    CGLTF_IMPLEMENTATION
    GLM_FORCE_RADIANS
    GLM_FORCE_DEPTH_ZERO_TO_ONE
    GLM_FORCE_LEFT_HANDED
)

# Copy shaders to build directory
add_custom_command(TARGET glb_renderer POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory $<TARGET_FILE_DIR:glb_renderer>/shaders
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_CURRENT_BINARY_DIR}/shaders
        $<TARGET_FILE_DIR:glb_renderer>/shaders
)

# Install
install(TARGETS glb_renderer RUNTIME DESTINATION bin)
