# Desktop simulator for StarDroid. Runs the exact same OS and app code as the
# watch, on top of LovyanGFX's SDL2 backend.
cmake_minimum_required(VERSION 3.16)
project(stardroid_sim CXX C)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(LovyanGFX
	GIT_REPOSITORY https://github.com/lovyan03/LovyanGFX.git
	GIT_TAG 1.2.7
	GIT_SHALLOW TRUE)
FetchContent_GetProperties(LovyanGFX)
if(NOT lovyangfx_POPULATED)
	FetchContent_Populate(LovyanGFX)
endif()
set(LGFX ${lovyangfx_SOURCE_DIR}/src)

find_package(SDL2 REQUIRED)

file(GLOB LGFX_SOURCES
	${LGFX}/lgfx/Fonts/efont/*.c
	${LGFX}/lgfx/Fonts/IPA/*.c
	${LGFX}/lgfx/utility/*.c
	${LGFX}/lgfx/v1/*.cpp
	${LGFX}/lgfx/v1/misc/*.cpp
	${LGFX}/lgfx/v1/panel/Panel_Device.cpp
	${LGFX}/lgfx/v1/panel/Panel_FrameBufferBase.cpp
	${LGFX}/lgfx/v1/platforms/sdl/*.cpp)
add_library(lgfx STATIC ${LGFX_SOURCES})
target_include_directories(lgfx PUBLIC ${LGFX} ${SDL2_INCLUDE_DIRS})
target_compile_definitions(lgfx PUBLIC LGFX_SDL LGFX_USE_V1)
target_compile_options(lgfx PRIVATE -w)
target_link_libraries(lgfx PUBLIC ${SDL2_LIBRARIES} pthread)

file(GLOB_RECURSE OS_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/../src/*.cpp)
add_executable(stardroid-sim sim_main.cpp ${OS_SOURCES})
target_include_directories(stardroid-sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_options(stardroid-sim PRIVATE -Wall -Wno-unused-parameter)
target_link_libraries(stardroid-sim PRIVATE lgfx)
