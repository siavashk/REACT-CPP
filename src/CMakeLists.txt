file(GLOB SRCS "**/*.cpp")
file(GLOB HDRS "**/*.h")

include_directories(${PROJECT_SOURCE_DIR}/include)
include_directories(/usr/local/include)
add_library(reactcpp ${SRCS} ${HDRS})
