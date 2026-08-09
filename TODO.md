# Stuff that needs to be done (eventually)

## CVAR System

- [ ] Optional bounds to scalar values (min and max for float and int).


## Logging System

- [ ] Add a logging system.
    - [ ] Plan out the subtasks for this.

## Build System & Architecture

- [ ] Refactor CMake shader compilation to automatically detect and compile new shaders without manual globbing/caching issues.
- [ ] Implement a proper virtual file system or path resolution system to avoid hardcoding relative paths (e.g., `../../shaders/...`) in engine code.

## Rendering

- [ ] Implement GPU-driven occlusion culling.