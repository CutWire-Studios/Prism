# Download a prebuilt ONNX Runtime release when it is not installed locally.
# On success sets ONNXRUNTIME_ROOT (and PRISM_ONNXRUNTIME_FETCHED).

if(PRISM_ONNXRUNTIME_FETCHED)
    return()
endif()

set(_onnxruntime_version "1.27.0")

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64|ARM64")
        set(_onnxruntime_archive "onnxruntime-linux-aarch64-${_onnxruntime_version}.tgz")
        set(_onnxruntime_sha256 "3e4d83ac06924a32a07b6d7f91ce6f852876153fc0bbdf931bf517a140bfbe48")
    elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
        set(_onnxruntime_archive "onnxruntime-linux-x64-${_onnxruntime_version}.tgz")
        set(_onnxruntime_sha256 "547e40a48f1fe73e3f812d7c88a948612c23f896b91e4e2ee1e232d7b468246f")
    endif()
elseif(APPLE)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "arm64|aarch64|ARM64")
        set(_onnxruntime_archive "onnxruntime-osx-arm64-${_onnxruntime_version}.tgz")
        set(_onnxruntime_sha256 "545e81c58152353acb0d1e8bd6ce4b62f830c0961f5b3acfedc790ffd76e477a")
    endif()
elseif(WIN32)
    if(CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|aarch64")
        set(_onnxruntime_archive "onnxruntime-win-arm64-${_onnxruntime_version}.zip")
        set(_onnxruntime_sha256 "a32f2650575b3c20df462e337519fd1cc4105356130d11dba9771c6f374d952f")
    else()
        set(_onnxruntime_archive "onnxruntime-win-x64-${_onnxruntime_version}.zip")
        set(_onnxruntime_sha256 "c5c81710938e68079ff1a192b04897faabe4b43830d48f39f27ecd4e16138bfc")
    endif()
endif()

if(NOT _onnxruntime_archive)
    message(FATAL_ERROR
        "Automatic ONNX Runtime download is not supported for "
        "${CMAKE_SYSTEM_NAME}/${CMAKE_SYSTEM_PROCESSOR}. "
        "Install ONNX Runtime manually and set ONNXRUNTIME_ROOT, "
        "or disable PRISM_WITH_SEGMENTATION.")
endif()

set(_onnxruntime_url
    "https://github.com/microsoft/onnxruntime/releases/download/v${_onnxruntime_version}/${_onnxruntime_archive}")

set(_onnxruntime_staging "${CMAKE_BINARY_DIR}/_deps/onnxruntime")

file(GLOB _onnxruntime_prefixes LIST_DIRECTORIES true
    "${_onnxruntime_staging}/onnxruntime-*")
if(_onnxruntime_prefixes)
    list(GET _onnxruntime_prefixes 0 _onnxruntime_prefix)
else()
    set(_onnxruntime_prefix "${_onnxruntime_staging}/${_onnxruntime_archive}")
    get_filename_component(_onnxruntime_prefix "${_onnxruntime_prefix}" NAME_WE)
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    file(MAKE_DIRECTORY "${_onnxruntime_staging}")
    set(_onnxruntime_archive_path "${_onnxruntime_staging}/${_onnxruntime_archive}")

    if(NOT EXISTS "${_onnxruntime_archive_path}")
        message(STATUS "Downloading ONNX Runtime ${_onnxruntime_version}: ${_onnxruntime_url}")
        file(DOWNLOAD "${_onnxruntime_url}" "${_onnxruntime_archive_path}"
            EXPECTED_HASH SHA256=${_onnxruntime_sha256}
            SHOW_PROGRESS)
    endif()

    file(ARCHIVE_EXTRACT
        INPUT "${_onnxruntime_archive_path}"
        DESTINATION "${_onnxruntime_staging}")
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    file(GLOB _onnxruntime_prefixes LIST_DIRECTORIES true
        "${_onnxruntime_staging}/onnxruntime-*")
    if(_onnxruntime_prefixes)
        list(GET _onnxruntime_prefixes 0 _onnxruntime_prefix)
    endif()
endif()

if(NOT EXISTS "${_onnxruntime_prefix}/include/onnxruntime_cxx_api.h")
    message(FATAL_ERROR
        "Failed to extract ONNX Runtime from ${_onnxruntime_archive}")
endif()

set(ONNXRUNTIME_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(OnnxRuntime_ROOT "${_onnxruntime_prefix}" CACHE PATH
    "ONNX Runtime install prefix (auto-downloaded)" FORCE)
set(PRISM_ONNXRUNTIME_FETCHED TRUE)
message(STATUS "ONNX Runtime ${_onnxruntime_version} ready at ${ONNXRUNTIME_ROOT}")
