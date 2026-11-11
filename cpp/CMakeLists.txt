cmake_minimum_required(VERSION 3.20)
project(ytgrab CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# A standalone binary: the MinGW runtime (libstdc++, winpthread) is linked in,
# so ytgrab.exe can be dropped next to the GUI without extra DLLs.
if(MINGW)
  add_link_options(-static -static-libgcc -static-libstdc++)
endif()

# ---------------------------------------------------------------------------
# vendored dependencies
# ---------------------------------------------------------------------------

add_library(sqlite3 STATIC third_party/sqlite/sqlite3.c)
target_include_directories(sqlite3 PUBLIC third_party/sqlite)
target_compile_definitions(sqlite3 PRIVATE
  SQLITE_THREADSAFE=1
  SQLITE_OMIT_LOAD_EXTENSION=1
  SQLITE_DQS=0
)

# ---------------------------------------------------------------------------
# core library (everything except main, so tests can link it)
# ---------------------------------------------------------------------------

add_library(ytgrab_core STATIC
  src/browser_cookies.cpp
  src/cli.cpp
  src/cookies.cpp
  src/download.cpp
  src/formats.cpp
  src/http.cpp
  src/innertube/browse.cpp
  src/innertube/client.cpp
  src/innertube/models.cpp
  src/innertube/player.cpp
  src/mux.cpp
  src/output.cpp
  src/runner.cpp
  src/subtitles.cpp
  src/url.cpp
  src/util.cpp
)

target_include_directories(ytgrab_core PUBLIC src third_party third_party/nlohmann)
target_compile_definitions(ytgrab_core PUBLIC
  NOMINMAX
  WIN32_LEAN_AND_MEAN
  UNICODE
  _UNICODE
  _WIN32_WINNT=0x0601
)
target_link_libraries(ytgrab_core PUBLIC sqlite3 winhttp crypt32 bcrypt)

if(MSVC)
  target_compile_options(ytgrab_core PRIVATE /W4 /permissive- /utf-8)
else()
  target_compile_options(ytgrab_core PRIVATE -Wall -Wextra -Wno-unused-parameter)
endif()

# ---------------------------------------------------------------------------
# executables
# ---------------------------------------------------------------------------

add_executable(ytgrab src/main.cpp)
target_link_libraries(ytgrab PRIVATE ytgrab_core)

add_executable(ytgrab_tests tests/test_main.cpp)
target_link_libraries(ytgrab_tests PRIVATE ytgrab_core)

enable_testing()
add_test(NAME unit COMMAND ytgrab_tests)
