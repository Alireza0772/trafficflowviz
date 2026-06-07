# FindONNXRuntime.cmake — fallback discovery for ONNX Runtime installs that ship raw
# headers + lib without a CMake config (manual downloads, some brew kegs). Prefer
# find_package(onnxruntime CONFIG) (vcpkg / official packages) when available.
#
# Honors -DONNXRUNTIME_ROOT=<dir> or the ONNXRUNTIME_ROOT environment variable, plus
# common system prefixes. Defines the imported target ONNXRuntime::ONNXRuntime.

set(_ort_hints "")
if(ONNXRUNTIME_ROOT)
    list(APPEND _ort_hints "${ONNXRUNTIME_ROOT}")
endif()
if(DEFINED ENV{ONNXRUNTIME_ROOT})
    list(APPEND _ort_hints "$ENV{ONNXRUNTIME_ROOT}")
endif()

find_path(ONNXRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS ${_ort_hints}
    PATHS /opt/homebrew /usr/local /opt/onnxruntime /usr
    PATH_SUFFIXES include include/onnxruntime include/onnxruntime/core/session
)

find_library(ONNXRuntime_LIBRARY
    NAMES onnxruntime
    HINTS ${_ort_hints}
    PATHS /opt/homebrew /usr/local /opt/onnxruntime /usr
    PATH_SUFFIXES lib lib64
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(ONNXRuntime
    REQUIRED_VARS ONNXRuntime_LIBRARY ONNXRuntime_INCLUDE_DIR
    FAIL_MESSAGE
        "ONNX Runtime not found. Install via vcpkg (onnxruntime), 'brew install onnxruntime', or pass -DONNXRUNTIME_ROOT=<dir>."
)

if(ONNXRuntime_FOUND AND NOT TARGET ONNXRuntime::ONNXRuntime)
    add_library(ONNXRuntime::ONNXRuntime UNKNOWN IMPORTED)
    set_target_properties(ONNXRuntime::ONNXRuntime PROPERTIES
        IMPORTED_LOCATION "${ONNXRuntime_LIBRARY}"
        INTERFACE_INCLUDE_DIRECTORIES "${ONNXRuntime_INCLUDE_DIR}"
    )
endif()

mark_as_advanced(ONNXRuntime_INCLUDE_DIR ONNXRuntime_LIBRARY)
