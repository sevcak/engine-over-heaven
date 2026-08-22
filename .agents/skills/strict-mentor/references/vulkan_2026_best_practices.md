# Vulkan 2026 Best Practices (SIGGRAPH 2026)

When reviewing Vulkan code, enforce these modern patterns and reject legacy approaches.

## 1. Pipeline and Shader Management
* **LEGACY:** `VkPipeline` (Fixed-state pipeline objects). Monolithic, rigid, and requires all state upfront.
* **MODERN:** **Shader Objects** (`VK_EXT_shader_object`). Separates shaders from graphics state. Shaders can be compiled separately and bound dynamically during command recording.

## 2. Descriptor Management
* **LEGACY:** Descriptor set layouts and pools.
* **MODERN:** **Descriptor Heaps** (`VK_EXT_descriptor_heap`). Uses a GPU-addressable buffer range containing descriptor bytes. The application places descriptors at explicit byte offsets. It provides direct control over placement and reuse without managing countless descriptor objects.

## 3. Synchronization
* **LEGACY:** Binary semaphores, fences, and manual layout transitions using old barriers.
* **MODERN:** 
    * **Timeline Semaphores** (`VK_KHR_timeline_semaphore` / Vulkan 1.2). A single semaphore represents many increasing milestone values.
    * **Synchronization2** (Vulkan 1.3). Keeps full barrier dependencies in a single clearer struct.
    * **Unified Image Layouts**. Fewer layout transitions to track.

## 4. Draws and Rendering (Render Passes)
* **LEGACY:** Render passes and framebuffers (`VkRenderPass`, `VkFramebuffer`).
* **MODERN:** **Dynamic Rendering** (`VK_KHR_dynamic_rendering` / Vulkan 1.3). Begin rendering directly with `vkCmdBeginRendering` using `VkRenderingInfo`. No render pass or framebuffer objects needed.

## 5. Subpasses and Local Reads
* **LEGACY:** Subpass input attachments and `vkCmdNextSubpass`.
* **MODERN:** **Dynamic Rendering Local Read** (Vulkan 1.4). Uses explicit by-region barriers instead of subpass dependencies. Read the input attachment at the current pixel using `SubpassLoad()`.

## 6. General Ecosystem
* **Abstraction Libraries:** Strongly recommend using `Vulkan-HPP` for typed C++ wrappers, `vk-bootstrap` for instance/device/swapchain setup, and `VMA` (Vulkan Memory Allocator) for allocations.
* **Shaders:** Use modern shader languages like `Slang` instead of raw GLSL where applicable.
* **Validation:** ALWAYS use Vulkan Validation Layers. Fix validation errors immediately. Use the Crash Diagnostic Layer (`VK_EXT_debug_utils`, `VK_KHR_device_fault`) for GPU crash debugging.
