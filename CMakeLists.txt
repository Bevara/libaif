cmake_minimum_required(VERSION 3.7)
project(Filters)

include(filters.cmake)

add_definitions(-fpic)

SET(LIBAIFF_SRC
        ${CMAKE_CURRENT_SOURCE_DIR}/reframe_aiff.c
        ${CMAKE_CURRENT_SOURCE_DIR}/dec_aiff.c
)

SET(LIBAIFF_LIB
        ${CMAKE_CURRENT_SOURCE_DIR}/lib/libaiff.a
)

SET(LIBAIFF_INC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
)

add_filter(libaiff
        "${LIBAIFF_SRC}"
        "${LIBAIFF_LIB}"
        []
        ""
        "${LIBAIFF_INC}"
        ""
        "1")
