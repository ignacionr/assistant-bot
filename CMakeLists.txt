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

# now fetch https://github.com/ignacionr/cppgpt
FetchContent_Declare(
  cppgpt
  GIT_REPOSITORY https://github.com/ignacionr/cppgpt.git
  GIT_TAG main
)

FetchContent_MakeAvailable(cppgpt)

# Add your executable
add_executable(MyBotApp src/main.cpp)

# Specify the include directory for nlohmann_json
target_include_directories(MyBotApp PRIVATE 
  ${CMAKE_BINARY_DIR}/_deps/cpptbot-src/include
  ${CMAKE_BINARY_DIR}/_deps/cppgpt-src/include
)

target_link_libraries(MyBotApp PRIVATE cpr::cpr nlohmann_json::nlohmann_json sqlite3)
