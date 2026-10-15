# Data-Oriented Design Rules

Design around the data and how it is transformed, not around objects. The hardware is the platform: cache lines, prefetchers, and branch predictors decide performance.

## Scope
* **Compulsory** on hot paths: anything executed per frame, per entity, per draw, or per element.
* **Encouraged** wherever many instances of the same thing exist.
* **Not enforced** for singletons and cold code (initialization, configuration, CVars, logging setup, tooling). Do not reject simple code there for not being data-oriented.

## 1. Know the Data First
Before judging any design, answer:
* **How many?** One, tens, thousands?
* **How often?** Once at load, per frame, per element per frame?
* **What is actually read and written** by the hot loop? Everything else is cold data.

If the author cannot answer these, the design is not ready.

## 2. Memory Layout
* **LEGACY:** Arrays of fat structs (AoS) where the hot loop touches only a few fields, which wastes cache lines.
* **MODERN:** Structure of Arrays (SoA), or hot/cold splitting: hot fields packed contiguously, rarely used fields moved to a parallel array.
* Prefer contiguous storage (`std::vector`, fixed arrays, spans) over node-based containers (`std::list`, `std::map`, `std::unordered_map`) on hot paths.
* Keep structs small and mind padding. Order members by size and avoid `bool` sprinkled between larger members.

## 3. References Between Data
* **LEGACY:** Object graphs of `std::shared_ptr` / raw pointers (e.g. a `Node` tree with `children` vectors). Pointer chasing, refcount traffic, scattered allocations.
* **MODERN:** Indices or generational handles into owning arrays. A single owner per array. Hierarchies (e.g. transforms) stored as flat arrays sorted parent-before-child so one linear pass resolves them.
* `std::unique_ptr` / `std::shared_ptr` are fine for ownership of systems and large resources at module boundaries, not for per-item graphs.

## 4. Dispatch and Branching
* **LEGACY:** `virtual` calls per item inside a loop (e.g. `node->draw()` over a heterogeneous tree).
* **MODERN:** Group items by type and process each group in its own tight loop. Sort by the key that matters (pipeline, material, mesh) before submission.
* **Existence-based processing:** being in an array *is* the state. Prefer separate arrays (e.g. `visible`, `dirty`) over per-item flags that the loop must test.
* Virtual interfaces are acceptable at coarse granularity (one call per system or per batch, not per item).

## 5. Allocation
* No heap allocations in steady-state per-frame code. Reserve up front, reuse buffers, use per-frame linear/arena allocators that reset each frame.
* No `std::string` construction, hashing of string keys, or `std::unordered_map<std::string, ...>` lookups on hot paths. Resolve names to handles at load time.

## 6. Transformations
* Write systems as functions that take input arrays and produce output arrays. Avoid hidden state and side effects.
* Batch work: one call that processes N items beats N calls that each process one.
* Data laid out this way is trivially parallelizable later (jobs over array ranges). Do not block that path with shared mutable state.

## 7. GPU Side
DOD applies to the GPU too, and aligns with the modern Vulkan rules:
* GPU-driven rendering: draw data in large contiguous buffers, indirect draws, culling in compute.
* Bindless / descriptor-heap access instead of per-object descriptor binding.
* Match CPU-side layouts to the GPU buffer layout so uploads are bulk copies, not per-object packing.

## 8. Evidence
Performance claims ("this is faster") should be backed by measurement (timings, captures). Without a profiler yet, at minimum reason explicitly about access patterns and cache behavior. Do not argue from intuition alone.
