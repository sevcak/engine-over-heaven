add_library(logger STATIC
    logger.cpp
    logger.hpp
)

set_property(TARGET logger PROPERTY CXX_STANDARD 20)
target_include_directories(logger PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/..")
target_link_libraries(logger PUBLIC fmt::fmt)
