#include "stereo_match_executorch.h"
#include "executorch_module_utils.h"
#include <vector>
#include <sstream>
#include <cstring>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <executorch/runtime/core/error.h>
#include <executorch/extension/module/module.h>
#include <executorch/extension/tensor/tensor.h>

using executorch::extension::Module;
using executorch::extension::from_blob;

static std::unique_ptr<Module> g_module;
static BANet_Config g_baCfg{};
static bool         g_ready = false;
static std::mutex   g_exec_mtx;

inline const std::string ba_status_to_string(BAStatus s) {
    switch (s) {
        case BAStatus::Ok:                  return "OK";
        case BAStatus::Busy:                return "Inference is busy.";
        case BAStatus::NotReady:            return "Module is not ready.";
        case BAStatus::RectEmpty:           return "Rect is empty.";
        case BAStatus::SizeMismatchLR:      return "Left/right sizes mismatch.";
        case BAStatus::RectTypeNot8UC3:     return "Rect type is not CV_8UC3.";
        case BAStatus::ForwardFailed:       return "Forward failed.";
        case BAStatus::OutputShapeMismatch: return "Forward result tensor shape mismatch.";
        default:                            return "Unknown error.";
    }
}

inline const std::string error_to_string(executorch::runtime::Error e) {
    using E = executorch::runtime::Error;
    switch (e) {
        case E::Ok:                             return "Ok";
        case E::Internal:                       return "An internal error occurred.";
        case E::InvalidState:                   return "The executor is in an invalid state.";
        case E::EndOfMethod:                    return "There are no more steps of execution to run.";
        case E::NotSupported:                   return "Operation is not supported in the current context.";
        case E::NotImplemented:                 return "Operation is not yet implemented.";
        case E::InvalidArgument:                return "User provided an invalid argument.";
        case E::InvalidType:                    return "Object is an invalid type for the operation.";
        case E::OperatorMissing:                return "Operator(s) missing in the operator registry.";
        case E::NotFound:                       return "Requested resource could not be found.";
        case E::MemoryAllocationFailed:         return "Could not allocate the requested memory.";
        case E::AccessFailed:                   return "Could not access a resource.";
        case E::InvalidProgram:                 return "Error caused by the contents of a program.";
        case E::InvalidExternalData:            return "Error caused by the contents of external data.";
        case E::OutOfResources:                 return "Does not have enough resources to perform the requested operation.";
        case E::DelegateInvalidCompatibility:   return "Init stage: Backend receives an incompatible delegate version.";
        case E::DelegateMemoryAllocationFailed: return "Init stage: Backend fails to allocate memory.";
        case E::DelegateInvalidHandle:          return "Execute stage: The handle is invalid.";
        default:                                return "Unknown";
    }
}

static inline void hwc8u_to_chw32f_rgb_norm_parallel(
    const cv::Mat& hwc_u8, float* __restrict chw,
    bool bgr_input, const float* mean, const float* std_)
{
    const int H = hwc_u8.rows, W = hwc_u8.cols;
    const int map[3] = { bgr_input ? 2 : 0, 1, bgr_input ? 0 : 2 };
    #pragma omp parallel for collapse(2)
    for (int c = 0; c < 3; ++c) {
        for (int y = 0; y < H; ++y) {
            const uint8_t* row = hwc_u8.ptr<uint8_t>(y);
            float* dst = chw + c * H * W + y * W;
            const int src_ch = map[c];
            const float m = mean ? mean[c] : 0.0f;
            const float s = std_ ? std_[c] : 1.0f;
            #pragma omp simd
            for (int x = 0; x < W; ++x) {
                const float f = row[x*3 + src_ch];
                dst[x] = (f - m) / s;
            }
        }
    }
}

std::string get_forward_meta_info() {
    std::lock_guard<std::mutex> lk(g_exec_mtx);
    return executorch_utils::dump_method_meta(*g_module);
}

bool init_stereo_executorch(const BANet_Config* cfg)
{
    if (!cfg) return false;
    std::lock_guard<std::mutex> lk(g_exec_mtx);
    g_baCfg = *cfg;
    try {
        g_module = std::make_unique<Module>(g_baCfg.modelPath);
        g_ready = true;
        return true;
    } catch (...) {
        g_ready = false;
        return false;
    }
}

void deinit_stereo_executorch() {
    std::lock_guard<std::mutex> lk(g_exec_mtx);
    g_module.reset();
    g_ready = false;
}

BAResult estimate_disparity_executorch(cv::Mat Lrect, cv::Mat Rrect)
{
    // Non-blocking: return Busy if another thread holds the lock.
    std::unique_lock<std::mutex> lk(g_exec_mtx, std::try_to_lock);
    if (!lk.owns_lock()) {
        return {cv::Mat(), BAStatus::Busy, ba_status_to_string(BAStatus::Busy)};
    }

    if (!g_ready || !g_module)                          return {cv::Mat(), BAStatus::NotReady, ba_status_to_string(BAStatus::NotReady)};
    if (Lrect.empty() || Rrect.empty())                 return {cv::Mat(), BAStatus::RectEmpty, ba_status_to_string(BAStatus::RectEmpty)};
    if (Lrect.size()!=Rrect.size())                     return {cv::Mat(), BAStatus::SizeMismatchLR, ba_status_to_string(BAStatus::SizeMismatchLR)};
    if (Lrect.type()!=CV_8UC3 || Rrect.type()!=CV_8UC3) return {cv::Mat(), BAStatus::RectTypeNot8UC3, ba_status_to_string(BAStatus::RectTypeNot8UC3)};

    const int H = Lrect.rows, W = Lrect.cols;
    const int inW = g_baCfg.inputW;
    const int inH = g_baCfg.inputH;

    const float scaleW = static_cast<float>(inW) / static_cast<float>(W);
    const float scaleH = static_cast<float>(inH) / static_cast<float>(H);

    cv::Mat Ls, Rs;
    cv::resize(Lrect, Ls, cv::Size(inW, inH), 0, 0, cv::INTER_AREA);
    cv::resize(Rrect, Rs, cv::Size(inW, inH), 0, 0, cv::INTER_AREA);

    std::vector<float> left(3 * inH * inW), right(3 * inH * inW);
    hwc8u_to_chw32f_rgb_norm_parallel(Ls, left.data(),  true, nullptr, nullptr);
    hwc8u_to_chw32f_rgb_norm_parallel(Rs, right.data(), true, nullptr, nullptr);

    auto tL = from_blob(left.data(),  {1, 3, inH, inW});
    auto tR = from_blob(right.data(), {1, 3, inH, inW});

    auto r = g_module->forward({tL, tR});
    if (!r.ok()) {
        return {cv::Mat(), BAStatus::ForwardFailed, std::string("Forward failed: ") + error_to_string(r.error())};
    }

    auto out = r->at(0).toTensor(); // expect (1,1,inH,inW)
    const auto& sz = out.sizes();
    if (sz.size()!=4 || sz[0]!=1 || sz[1]!=1 || sz[2]!=inH || sz[3]!=inW)
        return {cv::Mat(), BAStatus::OutputShapeMismatch, ba_status_to_string(BAStatus::OutputShapeMismatch)};

    const float* p = out.const_data_ptr<float>();
    cv::Mat disp_small(inH, inW, CV_32F);
    std::memcpy(disp_small.data, p, sizeof(float) * inH * inW);

    cv::Mat disp_full(H, W, CV_32F);
    cv::resize(disp_small, disp_full, cv::Size(W, H), 0, 0, cv::INTER_NEAREST);

    const float invScaleW = (scaleW != 0.0f) ? (1.0f / scaleW) : 1.0f;
    disp_full *= invScaleW;

    return {disp_full, BAStatus::Ok, ""};
}
