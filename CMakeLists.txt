cmake_minimum_required(VERSION 3.20)
project(loomlog VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# 源码使用 UTF-8 中文注释；MSVC 在中文系统（代码页 936）下默认按本地编码读取，
# 会让中文注释破坏后续 token。显式指定 /utf-8，保证在 Windows/MSVC 上也能构建。
if(MSVC)
    add_compile_options(/utf-8)
endif()

option(LOOMLOG_BUILD_TESTS "Build tests" ON)
option(LOOMLOG_BUILD_BENCH "Build benches" ON)
option(LOOMLOG_BUILD_CLI "Build CLI" ON)

# SQLite（vendor 的官方 amalgamation）为 FTS5 检索后端提供存储与查询能力。
add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/third_party/sqlite)
target_compile_definitions(sqlite3 PUBLIC
    SQLITE_ENABLE_FTS5
    SQLITE_THREADSAFE=1
    SQLITE_OMIT_LOAD_EXTENSION
)
if(MSVC)
    target_compile_options(sqlite3 PRIVATE /W0)
else()
    target_compile_options(sqlite3 PRIVATE -w)
endif()

add_library(loomlog
  src/thread_pool.cpp
  src/logger.cpp
  src/text_analyzer.cpp
  src/file_processor.cpp
  src/directory_scanner.cpp
  src/inverted_index.cpp
  src/indexer.cpp
  src/index_store.cpp
  src/query_engine.cpp
  src/sqlite_store.cpp
)
add_library(loomlog::loomlog ALIAS loomlog)
target_compile_features(loomlog PUBLIC cxx_std_20)
target_include_directories(loomlog
  PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)
target_link_libraries(loomlog PRIVATE sqlite3)

if(LOOMLOG_BUILD_CLI)
  add_executable(loomsearch src/cli.cpp)
  target_link_libraries(loomsearch PRIVATE loomlog)
endif()

if(LOOMLOG_BUILD_TESTS)
    enable_testing()

    include(FetchContent)

    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.15.2
    )

    # gtest_force_shared_crt only affects MSVC; harmless on other platforms.
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    add_executable(test_thread_pool tests/test_thread_pool.cpp)
    target_link_libraries(test_thread_pool PRIVATE loomlog GTest::gtest_main)

    add_executable(test_logger tests/test_logger.cpp)
    target_link_libraries(test_logger PRIVATE loomlog GTest::gtest_main)

    add_executable(test_text_analyzer tests/test_text_analyzer.cpp)
    target_link_libraries(test_text_analyzer PRIVATE loomlog GTest::gtest_main)

    add_executable(test_file_processor tests/test_file_processor.cpp)
    target_link_libraries(test_file_processor PRIVATE loomlog GTest::gtest_main)

    add_executable(test_directory_scanner tests/test_directory_scanner.cpp)
    target_link_libraries(test_directory_scanner PRIVATE loomlog GTest::gtest_main)

    add_executable(test_inverted_index tests/test_inverted_index.cpp)
    target_link_libraries(test_inverted_index PRIVATE loomlog GTest::gtest_main)

    add_executable(test_indexer tests/test_indexer.cpp)
    target_link_libraries(test_indexer PRIVATE loomlog GTest::gtest_main)

    add_executable(test_index_store tests/test_index_store.cpp)
    target_link_libraries(test_index_store PRIVATE loomlog GTest::gtest_main)

    add_executable(test_query_engine tests/test_query_engine.cpp)
    target_link_libraries(test_query_engine PRIVATE loomlog GTest::gtest_main)

    add_executable(test_sqlite_store tests/test_sqlite_store.cpp)
    target_link_libraries(test_sqlite_store PRIVATE loomlog GTest::gtest_main)

    if(LOOMLOG_BUILD_CLI)
        add_executable(test_cli tests/test_cli.cpp)
        target_link_libraries(test_cli PRIVATE GTest::gtest_main)
        target_compile_definitions(test_cli PRIVATE LOOMSEARCH_CLI_PATH="$<TARGET_FILE:loomsearch>")
        add_dependencies(test_cli loomsearch)
    endif()

    include(GoogleTest)
    gtest_discover_tests(test_thread_pool)
    gtest_discover_tests(test_logger)
    gtest_discover_tests(test_text_analyzer)
    gtest_discover_tests(test_file_processor)
    gtest_discover_tests(test_directory_scanner)
    gtest_discover_tests(test_inverted_index)
    gtest_discover_tests(test_indexer)
    gtest_discover_tests(test_index_store)
    gtest_discover_tests(test_query_engine)
    gtest_discover_tests(test_sqlite_store)
    if(LOOMLOG_BUILD_CLI)
        gtest_discover_tests(test_cli)
    endif()
endif()

if(LOOMLOG_BUILD_BENCH)
  add_executable(bench_pool benches/bench_pool.cpp)
  target_link_libraries(bench_pool PRIVATE loomlog)

  add_executable(bench_log benches/bench_log.cpp)
  target_link_libraries(bench_log PRIVATE loomlog)
endif()
