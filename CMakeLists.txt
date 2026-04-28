cmake_minimum_required(VERSION 3.14)

file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" CONNX_VERSION LIMIT_COUNT 1)

string(REGEX MATCH "^([0-9]+)\\.([0-9]+)\\.([0-9]+)" _ "${CONNX_VERSION}")
set(CONNX_VERSION_MAJOR "${CMAKE_MATCH_1}")
set(CONNX_VERSION_MINOR "${CMAKE_MATCH_2}")
set(CONNX_VERSION_PATCH "${CMAKE_MATCH_3}")

project(connx VERSION ${CONNX_VERSION})

message(STATUS "connx version: ${CONNX_VERSION}")

if(CMAKE_CXX_STANDARD)
    message(STATUS "Using external C++ Standard: ${CMAKE_CXX_STANDARD}")
else()
    set(CMAKE_CXX_STANDARD 11)
    message(STATUS "Using default C++ Standard: ${CMAKE_CXX_STANDARD}")
endif()

option(BUILD_STATIC "build static library" OFF)
option(BUILD_TESTS "build all unittests" OFF)
option(BUILD_EXAMPLES "build example programs" OFF)
option(BUILD_WITH_VERSION_SUFFIX "append version suffix to shared library" OFF)

if(WIN32)
    set(BASE_LIBRARIES wsock32 ws2_32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /W3")
else()
    set(BASE_LIBRARIES m pthread)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall")
endif()

aux_source_directory(${PROJECT_SOURCE_DIR}/src/bridge BRIDGE_SOURCE)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/codec CODEC_SOURCE)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/core CORE_SOURCE)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/net NET_SOURCE)
aux_source_directory(${PROJECT_SOURCE_DIR}/src/utils UTILS_SOURCE)
if(WIN32)
    list(REMOVE_ITEM NET_SOURCE ${PROJECT_SOURCE_DIR}/src/net/clientimpl_linux.cc)
else()
    list(REMOVE_ITEM NET_SOURCE ${PROJECT_SOURCE_DIR}/src/net/clientimpl_win32.cc)
endif()

# ============================================================================
# Object Library
# ============================================================================

add_library(connx_objects OBJECT
    ${BRIDGE_SOURCE}
    ${CODEC_SOURCE}
    ${CORE_SOURCE}
    ${NET_SOURCE}
    ${UTILS_SOURCE}
)

target_include_directories(connx_objects
PUBLIC
    $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include>
PRIVATE
    ${PROJECT_SOURCE_DIR}
)

target_compile_definitions(connx_objects
PUBLIC
    CONNX_VERSION_MAJOR=${CONNX_VERSION_MAJOR}
    CONNX_VERSION_MINOR=${CONNX_VERSION_MINOR}
    CONNX_VERSION_PATCH=${CONNX_VERSION_PATCH}
    CONNX_VERSION="${CONNX_VERSION}"
PRIVATE
    CONNX_COMPILE_LIBRARY
    $<$<CXX_COMPILER_ID:MSVC>:_WIN32_LEAN_AND_MEAN>
    $<$<CXX_COMPILER_ID:MSVC>:_UNICODE>
    $<$<CXX_COMPILER_ID:MSVC>:UNICODE>
    $<$<CXX_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<CXX_COMPILER_ID:MSVC>:_WINSOCK_DEPRECATED_NO_WARNINGS>
    $<$<CXX_COMPILER_ID:MSVC>:_NOMINMAX>
    $<$<NOT:$<BOOL:${BUILD_STATIC}>>:CONNX_SHARED_LIBRARY>
)

target_compile_options(connx_objects
PRIVATE
    $<$<NOT:$<CXX_COMPILER_ID:MSVC>>:-fvisibility=hidden>
)

set_target_properties(connx_objects PROPERTIES POSITION_INDEPENDENT_CODE ON)

# ============================================================================
# Library target
# ============================================================================

if(BUILD_STATIC)
    add_library(connx STATIC $<TARGET_OBJECTS:connx_objects>)
else()
    add_library(connx SHARED $<TARGET_OBJECTS:connx_objects>)
endif()

if (NOT BUILD_STATIC)
    if (BUILD_WITH_VERSION_SUFFIX)
        set_target_properties(connx PROPERTIES
            VERSION ${CONNX_VERSION}
            SOVERSION ${CONNX_VERSION_MAJOR}
        )
    endif()
endif()

target_link_libraries(connx PRIVATE ${BASE_LIBRARIES})

# ============================================================================
# Install
# ============================================================================

include(GNUInstallDirs)
install(TARGETS connx
    EXPORT connx-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

target_include_directories(connx
INTERFACE
    $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

install(DIRECTORY include/connx
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
    FILES_MATCHING PATTERN "*.h"
)

install(EXPORT connx-targets
    FILE connx-targets.cmake
    NAMESPACE connx::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/connx
)

# ============================================================================
# Tests
# ============================================================================

if(BUILD_TESTS)
    enable_testing()
    function(connx_add_test test_file)
        get_filename_component(target_name "${test_file}" NAME_WE)
        add_executable("${target_name}"
            "${test_file}"
            "${PROJECT_SOURCE_DIR}/test/util/testutil.cc"
        )
        # Only the integration test needs the echo server.
        if(target_name STREQUAL "test_integration")
            target_sources("${target_name}" PRIVATE
                "${PROJECT_SOURCE_DIR}/test/support/echoserver.cc"
            )
        endif()
        target_include_directories("${target_name}" PRIVATE "${PROJECT_SOURCE_DIR}")
        target_link_libraries("${target_name}" PRIVATE connx_objects ${BASE_LIBRARIES})
        add_test(NAME "${target_name}" COMMAND "${target_name}")
    endfunction()
    file(GLOB CONNX_TEST_SOURCES "${PROJECT_SOURCE_DIR}/test/test_*.cc")
    foreach(test_file ${CONNX_TEST_SOURCES})
        connx_add_test("${test_file}")
    endforeach()
endif()

# ============================================================================
# Examples
# ============================================================================

if(BUILD_EXAMPLES)
    # Server: standalone, does not depend on connx
    add_executable(server "${PROJECT_SOURCE_DIR}/example/server.cc")
    target_link_libraries(server PRIVATE ${BASE_LIBRARIES})
    if(NOT WIN32)
        target_compile_options(server PRIVATE -Wall)
    else()
        target_compile_definitions(server PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif()
    
    # C client
    add_executable(client_c "${PROJECT_SOURCE_DIR}/example/client_c.c")
    target_include_directories(client_c PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(client_c PRIVATE connx_objects ${BASE_LIBRARIES})

    # C++ client
    add_executable(client_cc "${PROJECT_SOURCE_DIR}/example/client_cc.cc")
    target_include_directories(client_cc PRIVATE "${PROJECT_SOURCE_DIR}")
    target_link_libraries(client_cc PRIVATE connx_objects ${BASE_LIBRARIES})
endif()
