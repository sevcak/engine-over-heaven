# Stuff that needs to be done (eventually)

## CVAR System

- [ ] Optional bounds to scalar values (min and max for float and int).


## Logging System

- [ ] Decouple formatting from I/O by implementing an `ILogSink` interface (e.g., ConsoleSink, FileSink).
- [ ] Route Error and Fatal logs to `stderr` instead of `stdout`.
- [ ] Implement Asynchronous Logging (background thread + lock-free queue) to prevent synchronous I/O blocking.
- [ ] Add Log Categories (Subsystems) to filter logs (e.g., `[Render]`, `[Physics]`).

## Build System & Architecture

- [ ] Refactor CMake shader compilation to automatically detect and compile new shaders without manual globbing/caching issues.
- [ ] Implement a proper virtual file system or path resolution system to avoid hardcoding relative paths (e.g., `../../shaders/...`) in engine code.

## Scene Management

- [ ] Implement support for Universal Scene Description (USD).

## Rendering

- [ ] Implement normal mapping.
- [ ] Implement support for dynamic scenes with moving objects.
- [ ] Implement GPU-driven occlusion culling.
- [ ] Migrate from monolithic `VkPipeline` to Shader Objects (`VK_EXT_shader_object`).
- [ ] Migrate from `VkDescriptorSet` management to Descriptor Heaps (`VK_EXT_descriptor_heap`).

## Refactor

- [ ] Decouple Material System from `VulkanRenderer`. Pass a smaller `RenderContext` or `DeviceContext` into `IMaterialType::build_pipelines` instead of the monolithic `VulkanRenderer*`.

## Profiling

- [ ] Set up Tracy and profiling for the project.