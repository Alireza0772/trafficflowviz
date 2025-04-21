#!/bin/bash
set -e

# Configuration
BUILD_TYPE="Debug"
BUILD_DIR="build"
INSTALL_DIR="install"
CLEAN=false
VERBOSE=false
INSTALL=false
INSTALL_DEPS=false

# Process arguments
while [[ $# -gt 0 ]]; do
  case $1 in
    --release)
      BUILD_TYPE="Release"
      shift
      ;;
    --clean)
      CLEAN=true
      shift
      ;;
    --verbose)
      VERBOSE=true
      shift
      ;;
    --install)
      INSTALL=true
      shift
      ;;
    --install-deps)
      INSTALL_DEPS=true
      shift
      ;;
    *)
      echo "Unknown option: $1"
      echo "Usage: $0 [--release] [--clean] [--verbose] [--install] [--install-deps]"
      exit 1
      ;;
  esac
done

# Check for dependencies
if [ "$INSTALL_DEPS" = true ]; then
  echo "Checking and installing dependencies..."
  
  if [ "$(uname)" == "Darwin" ]; then
    # macOS using Homebrew
    if ! command -v brew &> /dev/null; then
      echo "Homebrew not found. Please install Homebrew first."
      exit 1
    fi
    
    # Install SDL2 if not already installed
    if ! brew list sdl2 &> /dev/null; then
      echo "Installing SDL2..."
      brew install sdl2
    fi
    
    # Install GLM if not already installed
    if ! brew list glm &> /dev/null; then
      echo "Installing GLM..."
      brew install glm
    fi
    
    # Install pybind11 if not already installed
    if ! brew list pybind11 &> /dev/null; then
      echo "Installing pybind11..."
      brew install pybind11
    fi
  else
    # Linux
    echo "Please install SDL2, GLM, and pybind11 dependencies manually on your system."
    echo "For Ubuntu/Debian: sudo apt-get install libsdl2-dev libglm-dev pybind11-dev"
    echo "For Fedora: sudo dnf install SDL2-devel glm-devel pybind11-devel"
    exit 1
  fi
fi

# Check if GLM is available, regardless of the --install-deps flag
if [ "$(uname)" == "Darwin" ] && [ ! -f "/opt/homebrew/include/glm/glm.hpp" ]; then
  echo "GLM headers not found in expected location."
  echo "Installing GLM using Homebrew..."
  brew install glm
fi

# Run the include path fixer if it exists
if [ -f "fix_includes.sh" ]; then
  echo "Running include path fixer..."
  chmod +x fix_includes.sh
  ./fix_includes.sh
fi

# Clean build directory if requested
if [ "$CLEAN" = true ]; then
  echo "Cleaning build directory..."
  rm -rf "$BUILD_DIR"
fi

# Create build directory if it doesn't exist
mkdir -p "$BUILD_DIR"

# Navigate to build directory
cd "$BUILD_DIR"

# Configure with CMake
echo "Configuring project with CMake (Build type: $BUILD_TYPE)..."
CMAKE_ARGS="-DCMAKE_BUILD_TYPE=$BUILD_TYPE"

if [ "$INSTALL" = true ]; then
  CMAKE_ARGS="$CMAKE_ARGS -DCMAKE_INSTALL_PREFIX=../$INSTALL_DIR"
fi

cmake $CMAKE_ARGS ..

# Build the project
echo "Building project..."
if [ "$VERBOSE" = true ]; then
  cmake --build . --verbose
else
  cmake --build .
fi

# Install if requested
if [ "$INSTALL" = true ]; then
  echo "Installing to $INSTALL_DIR..."
  cmake --install .
fi

echo "Build completed successfully."
cd ..

# Display instructions
echo ""
echo "To run the application:"
echo "  $BUILD_DIR/bin/trafficviz"
echo ""
echo "To use Python bindings:"
echo "  export PYTHONPATH=$PWD/$BUILD_DIR/python:\$PYTHONPATH"
echo "  python scripts/simulations/rush_hour.py"
