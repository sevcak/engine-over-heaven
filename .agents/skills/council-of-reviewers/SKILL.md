---
name: council-of-reviewers
description: >-
  Orchestrates a Council of Reviewers to evaluate code changes. Uses subagents (C++ Idiom Expert, Vulkan Architect, Data-Oriented Performance Engineer, Architecture Reviewer) to scrutinize a given git diff or code snippet. Use this whenever the user wants a strict code review of their changes.
---

# Council of Reviewers

## Overview
This skill implements a "Council of Reviewers". Each reviewer runs as a separate subagent that scrutinizes the user's `git diff` or provided code snippet from one angle.

## Dependencies
None. Requires only the ability to run shell commands and spawn subagents.

## Workflow

### 1. Gathering Code
- If not provided, run `git diff` to capture the user's changes.
- Determine which changed code is on a hot path (per frame, per entity, per draw, per element) and tell every reviewer.

### 2. The Council of Reviewers
Spawn one subagent per reviewer, in parallel. Give each one the diff, the hot/cold classification, and its role below. Reference files live in `references/` next to this SKILL.md.

- **Subagent 1: C++ Idiom Expert.** Enforce modern C++: RAII, const correctness, value semantics, `std::span`/`std::string_view` for non-owning views, std algorithms and ranges. Smart pointers express ownership of systems and resources at module boundaries. Do NOT recommend `std::shared_ptr` graphs or per-item heap objects; per-item data belongs in contiguous arrays referenced by index/handle.
- **Subagent 2: Vulkan Architect.** Enforce modern Vulkan. Must read `references/vulkan_2026_best_practices.md`. Aggressively reject legacy patterns like `VkPipeline` and `VkRenderPass`.
- **Subagent 3: Data-Oriented Performance Engineer.** Must read `references/data_oriented_design.md`. Scrutinize memory layout, access patterns, pointer chasing, per-item virtual dispatch, per-frame allocations, and string-keyed lookups. Mandatory strictness on hot paths; on cold code and singletons, only flag issues that genuinely matter. Every finding must state the access pattern and why the cache or branch predictor suffers.
- **Subagent 4: Architecture Reviewer.** Review at the level of systems and modules, not classes. Focus on dependency direction, coupling between subsystems (e.g. passing a monolithic renderer pointer where a narrow context would do), clear data ownership, and API surface. Look for future scaling threats and propose decomposition strategies. Reject speculative abstraction: interfaces with one implementation, deep inheritance, getters/setters wrapping plain data, design patterns applied for their own sake. Simple, direct code is the goal.

### 3. Synthesis Phase
- Wait for all subagents to respond.
- Resolve conflicts with the project's design priorities: on hot paths, data-oriented design wins over idiom or architecture preferences; on cold paths, simplicity and readability win.
- Synthesize the critiques into a single, unified, strict review for the user, ordered by severity.
