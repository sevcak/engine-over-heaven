---
name: council-of-reviewers
description: >-
  Orchestrates a Council of Reviewers to evaluate code changes. Uses subagents (C++ Idiom Expert, Vulkan Architect, Clean Code Reviewer) to scrutinize a given git diff or code snippet. Use this whenever the user wants a strict code review of their changes.
---

# Council of Reviewers

## Overview
This skill implements a "Council of Reviewers". It uses the `invoke_subagent` and `define_subagent` tools to create subagents to scrutinize the user's `git diff` or provided code snippet.

## Dependencies
None. This skill uses built-in agentic tools (`invoke_subagent`, `run_command` with `git diff`).

## Workflow

### 1. Gathering Code
- If not provided, run `git diff` (using `run_command`) to capture the user's changes.

### 2. The Council of Reviewers
- Define (if not already defined) and invoke 3 subagents using the `define_subagent` and `invoke_subagent` tools.
- **Subagent 1: C++ Idiom Expert.** Role: Enforce modern C++ (smart pointers, const correctness, RAII, std algorithms).
- **Subagent 2: Vulkan Architect.** Role: Enforce modern Vulkan. Direct the subagent to read `references/vulkan_2026_best_practices.md` located next to this SKILL.md file. It must aggressively reject legacy patterns like `VkPipeline` and `VkRenderPass`.
- **Subagent 3: Clean Code Reviewer.** Role: Focus on readability, SOLID principles, decoupling. Must explicitly look for future scaling threats and suggest refactoring or decomposition strategies.
- Send the `git diff` or code to these subagents and ask for their critique.

### 3. Synthesis Phase
- Wait for all 3 subagents to respond.
- Synthesize their critiques into a single, unified, strict review for the user.
