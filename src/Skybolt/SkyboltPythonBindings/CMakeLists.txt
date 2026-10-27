set(TARGET_NAME skybolt)

add_source_group_tree(. SrcFiles)

include_directories("../")
include_directories("../../SkyboltExamples")

find_package(Python3 REQUIRED COMPONENTS Development Interpreter)
include_directories(${Python3_INCLUDE_DIRS})

find_package(pybind11 REQUIRED)
include_directories(${pybind11_INCLUDE_DIRS})

find_package(SDL2 REQUIRED) # joysticks of the manual aircraft, see FlightSimJoysticks.h

set(LIBS
	SkyboltEngine
	ExamplesCommon
	pybind11::headers
	SDL2::SDL2
)

if (WIN32)
	list(APPEND LIBS ${Python3_LIBRARIES})
	set(MODULE_TYPE SHARED) # the .pyd goes to bin/Release, where flightsim_env.py looks for it
else()
	# Extension modules must not link libpython on Linux. The interpreter provides the Python symbols, and
	# e.g. the Python of uv has libpython linked statically, so a second libpython would be loaded.
	set(MODULE_TYPE MODULE)
endif()

include(pybind11Tools)
pybind11_add_module(${TARGET_NAME} ${SrcFiles} ${MODULE_TYPE})
target_link_libraries (${TARGET_NAME} PUBLIC ${LIBS})

if (NOT WIN32)
	# The static SDL is not exported, so it cannot be mixed up with another SDL of the process, e.g. the one of pygame
	target_link_options(${TARGET_NAME} PRIVATE "LINKER:--exclude-libs,libSDL2.a")
endif()

set_target_properties(${TARGET_NAME} PROPERTIES FOLDER PythonLibs)

skybolt_python_module_install(${TARGET_NAME})