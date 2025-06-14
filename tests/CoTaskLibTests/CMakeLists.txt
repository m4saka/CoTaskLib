cmake_minimum_required(VERSION 3.16)
project(OpenSiv3D_Linux_App CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# FetchContentを使用してCatch2 v2.13.6を取得
include(FetchContent)
FetchContent_Declare(
  Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG        v2.13.6
)
FetchContent_MakeAvailable(Catch2)

if (NOT CMAKE_CONFIGURATION_TYPES AND 
    NOT CMAKE_NO_BUILD_TYPE AND
    NOT CMAKE_BUILD_TYPE AND
    CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    message(STATUS "[!] Setting build type to 'Release' as none was specified.")
    set(CMAKE_BUILD_TYPE Release)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_SOURCE_DIR})

add_executable(CoTaskLibTest
  Main.cpp
  )

target_include_directories(CoTaskLibTest PRIVATE
  ../../include
  )

find_package(Siv3D)
target_link_libraries(CoTaskLibTest PUBLIC Siv3D::Siv3D)

# Catch2をリンク
target_link_libraries(CoTaskLibTest PRIVATE Catch2::Catch2)

target_compile_features(CoTaskLibTest PRIVATE cxx_std_20)

# CTestサポートを有効化
enable_testing()
add_test(
  NAME Test
  COMMAND CoTaskLibTest
  WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
