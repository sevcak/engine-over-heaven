# Overheaven

A high-performance C++ game engine with a modern Vulkan renderer.

## Design Priorities
When these conflict, the higher one wins:

1. **Data-Oriented Design.** Mandatory on hot paths (anything that runs per frame, per entity, per draw, or per element). Elsewhere, apply it where it makes sense. Singletons and cold code (init, config, tooling) may stay simple. Rules: `.agents/skills/council-of-reviewers/references/data_oriented_design.md`.
2. **Modern Vulkan.** No legacy API patterns. Rules: `.agents/skills/council-of-reviewers/references/vulkan_2026_best_practices.md`.
3. **Simplicity.** Use the most direct code that solves today's problem. Don't add speculative abstractions or class hierarchies for their own sake.

## Documentation Comments

- Doc comments use the Doxygen/Javadoc form `/** ... */`. Never use `/* ... */` or `//` for documentation, because only `/**` gets doc-comment syntax highlighting.
- Every header says what it is responsible for. A header with a single type documents it on the type itself (`/** ... */` above the `struct`). A header with several types starts, right after `#pragma once`, with a 2–3 line `/** @file ... */` block. Without `@file`, Doxygen attaches the comment to the next declaration instead of to the file.
- Functions with parameters or a return value: `@brief` one-line summary, an optional detail paragraph, then `@tparam`, `@param` and `@return` as applicable. See the logging functions in `src/logger/logger.hpp`.
- Struct fields, constants and other short items: a one-line `/** ... */` is enough.
- Implementation comments explain how or why the code works, for whoever reads the body: inside functions, next to a tricky line, before a block of steps. They use `//` and are written as a sentence with a capital letter and a final period, e.g. `// Sort from farthest to nearest.`
