# Find ONNX Runtime (https://onnxruntime.ai)
#
# Set ONNXRUNTIME_ROOT to the install prefix if headers/libs are not on the
# default path (e.g. an extracted onnxruntime-linux-x64-*.tgz release).
#
# Defines:
#   OnnxRuntime_FOUND
#   OnnxRuntime_INCLUDE_DIRS
#   OnnxRuntime_LIBRARIES
#   ONNXRUNTIME_RUNTIME_LIBS  (shared libs to deploy next to the executable)
# and an imported target onnxruntime::onnxruntime.

find_path(OnnxRuntime_INCLUDE_DIR
    NAMES onnxruntime_cxx_api.h
    HINTS
        ${OnnxRuntime_ROOT}
        ${ONNXRUNTIME_ROOT}/include
        ${ONNXRUNTIME_ROOT}/include/onnxruntime
        ${ONNXRUNTIME_ROOT}/include/onnxruntime/core/session
        $ENV{ONNXRUNTIME_ROOT}/include
        /usr/include
        /usr/include/onnxruntime
        /usr/include/onnxruntime/core/session
        /usr/local/include
        /usr/local/include/onnxruntime
        /usr/local/include/onnxruntime/core/session
)

find_library(OnnxRuntime_LIBRARY
    NAMES onnxruntime
    HINTS
        ${OnnxRuntime_ROOT}
        ${ONNXRUNTIME_ROOT}/lib
        ${ONNXRUNTIME_ROOT}/lib64
        $ENV{ONNXRUNTIME_ROOT}/lib
        /usr/lib
        /usr/lib64
        /usr/local/lib
        /usr/lib/x86_64-linux-gnu
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(OnnxRuntime DEFAULT_MSG
    OnnxRuntime_LIBRARY OnnxRuntime_INCLUDE_DIR)

if(OnnxRuntime_FOUND)
    set(OnnxRuntime_INCLUDE_DIRS ${OnnxRuntime_INCLUDE_DIR})
    set(OnnxRuntime_LIBRARIES ${OnnxRuntime_LIBRARY})

    if(NOT TARGET onnxruntime::onnxruntime)
        add_library(onnxruntime::onnxruntime UNKNOWN IMPORTED)
        set_target_properties(onnxruntime::onnxruntime PROPERTIES
            IMPORTED_LOCATION "${OnnxRuntime_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OnnxRuntime_INCLUDE_DIR}")
    endif()

    # Shared runtime libraries (for deploy on Windows / packaging).
    file(GLOB ONNXRUNTIME_RUNTIME_LIBS
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.dll"
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.so*"
        "${ONNXRUNTIME_ROOT}/lib/*onnxruntime*.dylib")
endif()

mark_as_advanced(OnnxRuntime_INCLUDE_DIR OnnxRuntime_LIBRARY)
