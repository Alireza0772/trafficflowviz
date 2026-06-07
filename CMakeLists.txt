cmake_minimum_required(VERSION 3.25)
project(TrafficFlowViz VERSION 1.0.0 LANGUAGES C CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Enable FetchContent for dependency management
include(FetchContent)

# Define output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Build options
option(TFV_BUILD_PYTHON "Build Python bindings" ON)
option(TFV_WITH_PYTHON "Embed CPython for python:<module> brains" OFF)
option(TFV_USE_SANITIZERS "Enable address and undefined behavior sanitizers" OFF)
option(TFV_ENABLE_DOCS "Build documentation with Doxygen" OFF)
option(TFV_ENABLE_TESTING "Build unit tests" ON)
option(TFV_USE_VCPKG "Use vcpkg for dependency management" OFF)

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Python bindings: ${TFV_BUILD_PYTHON}")
message(STATUS "Embedded Python brains (TFV_WITH_PYTHON): ${TFV_WITH_PYTHON}")
message(STATUS "Sanitizers: ${TFV_USE_SANITIZERS}")
message(STATUS "Documentation: ${TFV_ENABLE_DOCS}")
message(STATUS "Testing: ${TFV_ENABLE_TESTING}")

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    # Modern C++ warnings
    add_compile_options(-Wall -Wextra -Wpedantic -Wno-unused-parameter)
    
    # Optimization flags
    if(CMAKE_BUILD_TYPE STREQUAL "Release")
        add_compile_options(-O3 -DNDEBUG -march=native)
    elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-O0 -g -DDEBUG)
    endif()
    
    # Sanitizers for debug builds
    if(TFV_USE_SANITIZERS AND CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(-fsanitize=address -fsanitize=undefined)
        add_link_options(-fsanitize=address -fsanitize=undefined)
    endif()
endif()


# Dependency management
if(TFV_USE_VCPKG)
    # Modern find_package approach with vcpkg
    find_package(SDL2 CONFIG REQUIRED)
    find_package(SDL2_image CONFIG REQUIRED)
    find_package(SDL2_ttf CONFIG REQUIRED)
    find_package(glm CONFIG REQUIRED)
    
    set(SDL2_LIBRARIES SDL2::SDL2 SDL2::SDL2main)
    set(SDL2_IMAGE_LIBRARIES SDL2_image::SDL2_image)
    set(SDL2_TTF_LIBRARIES SDL2_ttf::SDL2_ttf)
    set(GLM_INCLUDE_DIRS "") # glm is header-only and automatically handled by target
else()
    # Traditional find_package approach
    find_package(SDL2 REQUIRED)
    
    # Platform-specific SDL2 extensions
    if(APPLE)
        set(SDL2_IMAGE_LIBRARIES "/opt/homebrew/lib/libSDL2_image.dylib") 
        set(SDL2_TTF_LIBRARIES "/opt/homebrew/lib/libSDL2_ttf.dylib")
        set(SDL2_IMAGE_INCLUDE_DIRS "/opt/homebrew/include/SDL2")
        set(SDL2_TTF_INCLUDE_DIRS "/opt/homebrew/include/SDL2")
        set(GLM_INCLUDE_DIRS "/opt/homebrew/include")
        
        # Verify libraries exist
        foreach(lib ${SDL2_IMAGE_LIBRARIES} ${SDL2_TTF_LIBRARIES})
            if(NOT EXISTS "${lib}")
                message(FATAL_ERROR "Required library not found: ${lib}")
            endif()
        endforeach()
        
        if(NOT EXISTS "${GLM_INCLUDE_DIRS}/glm/glm.hpp")
            message(FATAL_ERROR "GLM library not found at ${GLM_INCLUDE_DIRS}")
        endif()
    else()
        # Use find_package for other platforms
        find_package(SDL2_image REQUIRED)
        find_package(SDL2_ttf REQUIRED)
        find_package(glm REQUIRED)
        set(GLM_INCLUDE_DIRS ${GLM_INCLUDE_DIR})
    endif()
endif()

# Fetch ImGui using FetchContent (docking branch for advanced features)
FetchContent_Declare(
  imgui
  GIT_REPOSITORY https://github.com/ocornut/imgui.git
  GIT_TAG        docking  # Use docking branch for advanced features
  GIT_SHALLOW    TRUE
)

FetchContent_MakeAvailable(imgui)

# Set ImGui directory for compatibility
set(IMGUI_DIR ${imgui_SOURCE_DIR})


# Note: Configuration is now handled at runtime via Configuration class
# No need for compile-time configuration anymore

# Set include paths
include_directories(
    ${SDL2_INCLUDE_DIRS}
    ${SDL2_IMAGE_INCLUDE_DIRS}
    ${SDL2_TTF_INCLUDE_DIRS}
    ${GLM_INCLUDE_DIRS}
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${IMGUI_DIR}
    ${IMGUI_DIR}/backends
)

# Print include directories for debugging
get_property(dirs DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES)
foreach(dir ${dirs})
  message(STATUS "Include directory: ${dir}")
endforeach()

# Add subdirectories
add_subdirectory(src)

# Conditional Python bindings
if(TFV_BUILD_PYTHON)
    add_subdirectory(bindings)
endif()

# Optional testing
if(TFV_ENABLE_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Optional documentation
if(TFV_ENABLE_DOCS)
    find_package(Doxygen)
    if(DOXYGEN_FOUND)
        # TODO: Configure Doxygen
        message(STATUS "Doxygen found - documentation can be built")
    else()
        message(WARNING "Doxygen not found - documentation cannot be built")
    endif()
endif()

# Set installation paths
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/data/
        DESTINATION ${CMAKE_INSTALL_PREFIX}/data
        FILES_MATCHING PATTERN "*.csv")

# Install scripts
# install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/scripts/
#         DESTINATION ${CMAKE_INSTALL_PREFIX}/scripts
#         FILES_MATCHING PATTERN "*.py"
#         PERMISSIONS OWNER_EXECUTE OWNER_WRITE OWNER_READ
#                     GROUP_EXECUTE GROUP_READ
#                     WORLD_EXECUTE WORLD_READ)
