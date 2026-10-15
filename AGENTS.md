# Overheaven

A high-performance C++ game engine with a modern Vulkan renderer.

## Design Priorities
When these conflict, the higher one wins:

1. **Data-Oriented Design.** Mandatory on hot paths (anything that runs per frame, per entity, per draw, or per element). Elsewhere, apply it where it makes sense. Singletons and cold code (init, config, tooling) may stay simple. Rules: `.agents/skills/council-of-reviewers/references/data_oriented_design.md`.
2. **Modern Vulkan.** No legacy API patterns. Rules: `.agents/skills/council-of-reviewers/references/vulkan_2026_best_practices.md`.
3. **Simplicity.** Use the most direct code that solves today's problem. Don't add speculative abstractions or class hierarchies for their own sake.
