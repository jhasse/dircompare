cmake_minimum_required (VERSION 3.8)

project(dircompare LANGUAGES CXX)

file(GLOB SRC src/*.cpp)
add_executable(dircompare ${SRC})

target_compile_features(dircompare PUBLIC cxx_std_17)

if(NOT MSVC)
	set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fdiagnostics-color")
endif()
