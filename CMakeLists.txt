cmake_minimum_required(VERSION 3.7)

if(NOT DEFINED CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS 1 CACHE INTERNAL "No dev warnings")
endif()

project(conway C)
list(APPEND CMAKE_MODULE_PATH ${CMAKE_CURRENT_SOURCE_DIR}/cmake/sdl2)

find_package(SDL2 REQUIRED)

set(MPI_DIR "/usr/lib64/mpich")
set(MPI_INCLUDE_PATH, "/usr/include/mpich-x86_64/mpi.h")
set(MPI_C_LIBRARIES, "/usr/lib64/mpich/lib")
find_package(MPI REQUIRED)

include_directories(${MPI_INCLUDE_PATH})

set(SRCS
    main.c
    logic.c
    render.c
)

set(HEADERS
    logic.h
    render.h
    game.h
    mpi_logic.h
)

set(CMAKE_SYSTEM_PREFIX_PATH ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(conway ${SRCS} ${HEADERS})
target_link_libraries(conway SDL2::Main ${MPI_C_LIBRARIES})
install(TARGETS ${PROJECT_NAME} DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})

