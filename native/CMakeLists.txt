cmake_minimum_required(VERSION 3.22)
project(pc_stereo_ndk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(ANDROID_ABI "arm64-v8a")
set(ANDROID_PLATFORM android-26)

# Paths
set(OPENCV_INC ${CMAKE_SOURCE_DIR}/third_party/opencv/jni/include)
set(OPENCV_LIB ${CMAKE_SOURCE_DIR}/third_party/opencv/libs/${ANDROID_ABI})
set(EXECUTORCH_INC ${CMAKE_SOURCE_DIR}/third_party/executorch/jni/include)
set(EXECUTORCH_LIB_DIR ${CMAKE_SOURCE_DIR}/third_party/executorch/libs/${ANDROID_ABI})
set(PTHREADPOOL_INC ${EXECUTORCH_INC}/executorch/backends/xnnpack/third-party/pthreadpool/include)

include_directories(
    ${OPENCV_INC} 
    ${EXECUTORCH_INC}
    ${PTHREADPOOL_INC}
)

add_library(pc_stereo_ndk SHARED
    stereo_cam_ndk.cpp
    stereo_match_sgbm.cpp
    stereo_match_executorch.cpp
    executorch_module_utils.cpp
    pc_stereo.cpp
)

target_link_libraries(pc_stereo_ndk
    -Wl,--start-group
    -Wl,--whole-archive
      ${EXECUTORCH_LIB_DIR}/libxnnpack_backend.a
      ${EXECUTORCH_LIB_DIR}/libportable_ops_lib.a
    -Wl,--no-whole-archive
      ${EXECUTORCH_LIB_DIR}/libXNNPACK.a
      ${EXECUTORCH_LIB_DIR}/libxnnpack-microkernels-prod.a
      ${EXECUTORCH_LIB_DIR}/libpthreadpool.a
      ${EXECUTORCH_LIB_DIR}/libportable_kernels.a
      ${EXECUTORCH_LIB_DIR}/libcpuinfo.a
      ${EXECUTORCH_LIB_DIR}/libcpuinfo_internals.a
      ${EXECUTORCH_LIB_DIR}/libkernels_util_all_deps.a
      ${EXECUTORCH_LIB_DIR}/libexecutorch_core.a
      ${EXECUTORCH_LIB_DIR}/libexecutorch.a

      ${EXECUTORCH_LIB_DIR}/libextension_module.a
      ${EXECUTORCH_LIB_DIR}/libextension_tensor.a
      ${EXECUTORCH_LIB_DIR}/libextension_flat_tensor.a
      ${EXECUTORCH_LIB_DIR}/libextension_data_loader.a
      ${EXECUTORCH_LIB_DIR}/libextension_threadpool.a

      ${EXECUTORCH_LIB_DIR}/libkleidiai.a
    -Wl,--end-group

    ${OPENCV_LIB}/libopencv_core.so
    ${OPENCV_LIB}/libopencv_imgproc.so
    ${OPENCV_LIB}/libopencv_calib3d.so
    ${OPENCV_LIB}/libopencv_ximgproc.so
    camera2ndk
    mediandk
    android
    log
    c++_shared
)

set_target_properties(pc_stereo_ndk PROPERTIES
    LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/out/${ANDROID_ABI}
)

target_link_options(pc_stereo_ndk PRIVATE
    -Wl,-rpath-link,${OPENCV_LIB}
    -Wl,--no-as-needed
    -Wl,-rpath-link,${EXECUTORCH_LIB_DIR}
    -Wl,--as-needed
)
