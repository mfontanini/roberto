include_directories(${CMAKE_CURRENT_SOURCE_DIR}/../include)

set(SOURCES
    server.cpp
    client_connection.cpp
    channel.cpp
    utils.cpp
)

add_library(roberto-internal ${SOURCES})

add_executable(roberto main.cpp)
target_link_libraries(roberto roberto-internal log4cxx)
