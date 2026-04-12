cmake_minimum_required(VERSION 3.10)
project(TradingPlatform VERSION 1.0.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Debug flags
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -O0 -Wall -Wextra -Werror")
else()
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O2")
endif()

# Find nlohmann/json
find_package(nlohmann_json 3.9.1 REQUIRED)
find_package(CURL REQUIRED)

# Include directories
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${CURL_INCLUDE_DIRS})

# ===================== LIBRARIES =====================
# Create storage library
add_library(storage_lib
    storage/FileManager.cpp
    storage/Page.cpp
    storage/BufferManager.cpp
    storage/WAL.cpp
)

# Create btree library
add_library(btree_lib
    btree/BTree.cpp
    btree/BPlusTree.cpp
)

# Link btree library with storage
target_link_libraries(btree_lib storage_lib)

# ===================== DAY 5 - COMPLETE TRADING PLATFORM =====================
add_executable(trading_platform_day5
    main.cpp  # This is your Day 5 main with HttpServer
    # Storage layer
    storage/FileManager.cpp
    storage/Page.cpp
    storage/BufferManager.cpp
    storage/WAL.cpp
    # Indexing layer
    btree/BPlusTree.cpp
    btree/BTree.cpp
    index/HashIndex.cpp
    # Day 4 - Core trading engine
    core/OrderBook.cpp
    core/MatchingEngine.cpp
    core/UserManager.cpp
    # Day 5 - API layer
    api/BackendAPI.cpp
    # Day 5 - HTTP Server
    server/HttpServer.cpp
)

# Link libraries for Day 5 executable
target_link_libraries(trading_platform_day5
    ${CMAKE_THREAD_LIBS_INIT}
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
)

# ===================== DAY 1-4 BASIC VERSION =====================
add_executable(trading_platform
    main_day4.cpp  # Separate main for Day 1-4
    storage/FileManager.cpp
    storage/Page.cpp
    storage/BufferManager.cpp
    storage/WAL.cpp
    btree/BPlusTree.cpp
    btree/BTree.cpp
    index/HashIndex.cpp
    # Day 4 - Core trading engine
    core/OrderBook.cpp
    core/MatchingEngine.cpp
    core/UserManager.cpp
)

target_link_libraries(trading_platform
    ${CMAKE_THREAD_LIBS_INIT}
)

# ===================== TESTS =====================
add_executable(storage_test
    test/storage_test.cpp
)

target_link_libraries(storage_test storage_lib)

add_executable(simple_buffer_test
    test/simple_buffer_test.cpp
)

target_link_libraries(simple_buffer_test storage_lib)

add_executable(btree_test
    test/btree_test.cpp
)

target_link_libraries(btree_test btree_lib storage_lib)

add_executable(day3_test
    test/day3_test.cpp
    index/HashIndex.cpp
)

target_link_libraries(day3_test btree_lib storage_lib ${CMAKE_THREAD_LIBS_INIT})

add_executable(day4_test
    test/day4_test.cpp
    core/OrderBook.cpp
    core/MatchingEngine.cpp
    core/UserManager.cpp
)

target_link_libraries(day4_test
    btree_lib
    storage_lib
    ${CMAKE_THREAD_LIBS_INIT}
)

# ===================== DAY 5 TEST =====================
add_executable(day5_test
    test/day5_test.cpp
    api/BackendAPI.cpp
    server/HttpServer.cpp
    # Include all dependencies
    storage/FileManager.cpp
    storage/Page.cpp
    storage/BufferManager.cpp
    storage/WAL.cpp
    btree/BPlusTree.cpp
    btree/BTree.cpp
    index/HashIndex.cpp
    core/OrderBook.cpp
    core/MatchingEngine.cpp
    core/UserManager.cpp
)

target_link_libraries(day5_test
    btree_lib
    storage_lib
    ${CMAKE_THREAD_LIBS_INIT}
    nlohmann_json::nlohmann_json
    ${CURL_LIBRARIES}
)

# ===================== CONVENIENCE TARGETS =====================
add_custom_target(run_day5
    COMMAND ./trading_platform_day5
    DEPENDS trading_platform_day5
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running Day 5 Trading Platform..."
)

add_custom_target(test_all
    COMMAND echo "Running all tests..." &&
            ./storage_test &&
            ./btree_test &&
            ./day3_test &&
            ./day4_test &&
            ./day5_test
    DEPENDS storage_test btree_test day3_test day4_test day5_test
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Running all test suites..."
)

# ===================== FILE COPY FOR FRONTEND =====================
# Ensure frontend directory exists
file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/frontend)

# Copy all frontend files
file(GLOB_RECURSE FRONTEND_FILES 
    "${CMAKE_SOURCE_DIR}/frontend/*"
)
foreach(file ${FRONTEND_FILES})
    file(RELATIVE_PATH rel_file ${CMAKE_SOURCE_DIR}/frontend ${file})
    get_filename_component(rel_dir ${rel_file} DIRECTORY)
    if(rel_dir)
        file(MAKE_DIRECTORY ${CMAKE_BINARY_DIR}/frontend/${rel_dir})
    endif()
    file(COPY ${file} DESTINATION ${CMAKE_BINARY_DIR}/frontend/${rel_dir})
endforeach()

message(STATUS "Frontend files copied to: ${CMAKE_BINARY_DIR}/frontend")