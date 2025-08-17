#include "stereo_cam_ndk.h"
#include <cstring>
#include <opencv2/core.hpp>

enum class BAStatus : int {
    Ok = 0,
    NotReady,
    Busy,
    RectEmpty,
    SizeMismatchLR,
    RectTypeNot8UC3,
    ForwardFailed,
    OutputShapeMismatch
};

struct BAResult {
    cv::Mat image;
    BAStatus status;
    std::string errorMsg;
};

bool init_stereo_executorch(const BANet_Config* cfg);
void deinit_stereo_executorch();
std::string get_forward_meta_info();
BAResult estimate_disparity_executorch(cv::Mat Lrect, cv::Mat Rrect);
