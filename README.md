# glb_renderer

A Vulkan-based glTF/GLB renderer in C++20 with bindless textures, Cook-Torrance GGX PBR, and image-based lighting.

## Features

- Loads and renders glTF 2.0 binary (.glb) and JSON (.gltf) models
- PBR metallic-roughness workflow (Cook-Torrance GGX with Smith visibility)
- Bindless descriptors: up to 1024 textures, 16 cubemaps, no per-material descriptor rebinds
- CPU-generated IBL: gradient skybox cubemap, irradiance, prefiltered environment, BRDF LUT
- ACES filmic tone mapping
- Orbit camera with mouse drag rotation and scroll wheel zoom
- Frustum culling
- Multi-material support
- SRGB-correct texture loading (base color, emissive) with mipmap generation

## Dependencies

- Vulkan SDK 1.4+ (with `glslc` in PATH)
- GLFW3
- CMake 3.20+
- C++20 compiler

All other dependencies (cgltf, GLM, vk-bootstrap, VMA, stb_image) are fetched automatically via FetchContent.

## Building

```sh
cmake -B build
cmake --build build
```

The binary is placed at `build/glb_renderer`.

## Usage

```sh
./build/glb_renderer path/to/model.glb
```

### Controls

| Input | Action |
|---|---|
| Left mouse drag | Orbit camera |
| Scroll wheel | Zoom in/out |
| + / - keys | Zoom in/out |
| Escape | Quit |

## Shaders

All shaders are in `shaders/` and compiled at build time by `glslc`. The key shaders are:

- `pbr.vert` / `pbr.frag` -- PBR shading with direct lighting and IBL
- `skybox.vert` / `skybox.frag` -- Procedural gradient skybox overlay
- `vertex.vert` / `fragment.frag` -- Flat color pipeline (fallback for non-PBR materials)

## Project Structure

```
shaders/          GLSL shaders (compiled to SPIR-V at build time)
src/              C++ source
  buffer.cpp/hpp     RAII VkBuffer with VMA staging
  camera.cpp/hpp     Orbit camera controller
  descriptor_set.cpp/hpp  Bindless descriptor manager
  frustum.cpp/hpp    Frustum plane extraction and AABB culling
  gltf_loader.cpp/hpp  cgltf wrapper (scene graph, materials, textures)
  ibl.cpp/hpp        CPU-based IBL generation (skybox, irradiance, prefiltered, BRDF LUT)
  image.cpp/hpp      RAII VkImage with VMA allocation
  indirect_draw.cpp/hpp  VkDrawIndexedIndirect buffer manager
  material.hpp       PBR material struct
  mesh.cpp/hpp       Interleaved vertex buffer upload
  pipeline.cpp/hpp   ShaderModule and GraphicsPipeline builder
  profiler.cpp/hpp   Frame rate counter
  renderer.cpp/hpp   Render orchestrator (scene upload, pipelines, frame loop)
  sampler.cpp/hpp    VkSampler creation
  scene.cpp/hpp      Node hierarchy, drawable list, AABB bounds
  shader_path.hpp    Runtime shader path resolution
  skin.cpp/hpp       GPU skinning SSBO upload (in progress)
  texture.cpp/hpp    stb_image decode, GPU upload with mipmaps
  vulkan_context.cpp/hpp  Instance, device, swapchain, VMA, per-frame sync
```

## Test Models

Place glTF/GLB files in `test_models/`. Several Khronos sample models are included (Box, DamagedHelmet, BoomBox, AntiqueCamera, BrainStem).
