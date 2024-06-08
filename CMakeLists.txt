cmake_minimum_required(VERSION 3.14)
project(MyBotApp VERSION 1.0 LANGUAGES CXX)

# Specify the C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

# Set to build static libraries
set(BUILD_SHARED_LIBS OFF)

# Ensure we are using static linking
set(CMAKE_FIND_LIBRARY_SUFFIXES ".a")
set(CMAKE_EXE_LINK_DYNAMIC_C_FLAGS)  # empty
set(CMAKE_EXE_LINK_DYNAMIC_CXX_FLAGS)  # empty
set(CMAKE_SHARED_LIBRARY_C_FLAGS)  # empty
set(CMAKE_SHARED_LIBRARY_CXX_FLAGS)  # empty
set(CMAKE_EXE_LINKER_FLAGS_RELEASE "-static-libgcc -static-libstdc++")

# Include FetchContent module
include(FetchContent)

# Fetch cppTBot from GitHub
FetchContent_Declare(
  cppTBot
  GIT_REPOSITORY https://github.com/ignacionr/cpptbot.git
  GIT_TAG main  # You can specify a particular commit, tag, or branch here
)

# Make cppTBot available
FetchContent_MakeAvailable(cppTBot)

# Fetch cppgpt
FetchContent_Declare(
  cppgpt
  GIT_REPOSITORY https://github.com/ignacionr/cppgpt.git
  GIT_TAG main
)

FetchContent_MakeAvailable(cppgpt)

# Fetch Google Test
FetchContent_Declare(
  googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG release-1.12.1  # This is an example tag; you can select the appropriate version
)
FetchContent_MakeAvailable(googletest)

# Enable testing in CMake
enable_testing()

# Add your application's executable
add_executable(MyBotApp src/main.cpp)

# Specify the include directory
include_directories( 
  ${CMAKE_BINARY_DIR}/_deps/cpptbot-src/include
  ${CMAKE_BINARY_DIR}/_deps/cppgpt-src/include
)

target_link_libraries(MyBotApp PRIVATE cpr::cpr nlohmann_json::nlohmann_json sqlite3)

# Add the test executable
add_executable(test_section_parsing tests/section-parsing.cpp)

# Link the test executable with GoogleTest and any other required libraries
target_link_libraries(test_section_parsing PRIVATE 
  gtest_main 
  gmock_main 
  nlohmann_json::nlohmann_json
  cpr::cpr
  )

# Add the test to CTest so it can be run with `make test` or `ctest`
include(GoogleTest)
gtest_discover_tests(test_section_parsing)
