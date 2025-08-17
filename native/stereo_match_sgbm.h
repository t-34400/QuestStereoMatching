#include "stereo_cam_ndk.h"
#include <opencv2/core.hpp>

void init_stereo_sgbm(const SGBM_Config* sgbmCfg);
cv::Mat estimate_disparity_sgbm(cv::Mat Lrect, cv::Mat Rrect);