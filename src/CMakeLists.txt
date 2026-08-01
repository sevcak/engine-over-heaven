add_subdirectory(core)
add_subdirectory(camera)
add_subdirectory(renderer/vulkan)
add_subdirectory(engine)


add_executable(engine main.cpp)

set_property(TARGET engine PROPERTY CXX_STANDARD 20)
target_include_directories(engine PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(engine PRIVATE engine_lib)

target_precompile_headers(engine PUBLIC <optional> <vector> <memory> <string> <vector> <unordered_map> <glm/mat4x4.hpp> <glm/vec4.hpp> <vulkan/vulkan.h>)

if(WIN32)
  add_custom_command(TARGET engine POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_RUNTIME_DLLS:engine> $<TARGET_FILE_DIR:engine>
    COMMAND_EXPAND_LISTS
    )
endif()