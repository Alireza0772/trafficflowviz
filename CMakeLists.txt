cmake_minimum_required(VERSION 3.12)
project(TrafficFlowViz VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Define output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Set build type
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")


# Find required packages
find_package(SDL2 REQUIRED)

# Add ImGui sources
set(IMGUI_DIR ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
if(NOT EXISTS "${IMGUI_DIR}")
  message(STATUS "ImGui not found, will download it...")
  # Clone the ImGui repository
  execute_process(
    COMMAND git clone --depth 1 https://github.com/ocornut/imgui.git ${IMGUI_DIR}
    RESULT_VARIABLE GIT_RESULT
  )
  if(NOT GIT_RESULT EQUAL 0)
    message(FATAL_ERROR "Failed to clone ImGui repository. Please install git or download ImGui manually.")
  endif()
endif()

# For SDL2_image and SDL2_ttf on macOS, point directly to the libraries
if(APPLE)
  set(SDL2_IMAGE_LIBRARIES "/opt/homebrew/lib/libSDL2_image.dylib") 
  set(SDL2_TTF_LIBRARIES "/opt/homebrew/lib/libSDL2_ttf.dylib")
  set(SDL2_IMAGE_INCLUDE_DIRS "/opt/homebrew/include/SDL2")
  set(SDL2_TTF_INCLUDE_DIRS "/opt/homebrew/include/SDL2")
  
  if(NOT EXISTS "${SDL2_IMAGE_LIBRARIES}")
    message(FATAL_ERROR "SDL2_image library not found at ${SDL2_IMAGE_LIBRARIES}. Please install SDL2_image.")
  else()
    message(STATUS "Found SDL2_image: ${SDL2_IMAGE_LIBRARIES}")
  endif()
  
  if(NOT EXISTS "${SDL2_TTF_LIBRARIES}")
    message(FATAL_ERROR "SDL2_ttf library not found at ${SDL2_TTF_LIBRARIES}. Please install SDL2_ttf.")
  else()
    message(STATUS "Found SDL2_ttf: ${SDL2_TTF_LIBRARIES}")
  endif()
else()
  # Use find_package for other platforms
  find_package(SDL2_image REQUIRED)
  find_package(SDL2_ttf REQUIRED)
endif()

# Set GLM include path directly since find_package isn't working
set(GLM_INCLUDE_DIRS "/opt/homebrew/include")
if(NOT EXISTS "${GLM_INCLUDE_DIRS}/glm/glm.hpp")
  message(FATAL_ERROR "GLM library not found at ${GLM_INCLUDE_DIRS}. Please install GLM.")
else()
  message(STATUS "Found GLM: ${GLM_INCLUDE_DIRS}")
endif()

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
add_subdirectory(bindings)

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
