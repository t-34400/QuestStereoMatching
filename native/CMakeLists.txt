cmake_minimum_required(VERSION 3.22)
project(pc_stereo_ndk LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(ANDROID_ABI "arm64-v8a")
set(ANDROID_PLATFORM android-26)

# OpenCV
set(OPENCV_INC ${CMAKE_SOURCE_DIR}/third_party/opencv/jni/include)
set(OPENCV_LIB ${CMAKE_SOURCE_DIR}/third_party/opencv/libs/${ANDROID_ABI})

include_directories(${OPENCV_INC})
link_directories(${OPENCV_LIB})

add_library(pc_stereo_ndk SHARED
    stereo_cam_ndk.cpp
    pc_stereo.cpp
)

target_link_libraries(pc_stereo_ndk
    opencv_core
    opencv_imgproc
    opencv_calib3d
    opencv_ximgproc
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
)