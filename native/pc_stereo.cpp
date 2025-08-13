#include "stereo_cam_ndk.h"              // API surface:contentReference[oaicite:1]{index=1}
#include <thread>
#include <atomic>
#include <condition_variable>
#include <vector>
#include <mutex>
#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/ximgproc/disparity_filter.hpp>

#define LOGE_TAG "StereoNDK"
#include <android/log.h>
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOGE_TAG, __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  LOGE_TAG, __VA_ARGS__)

namespace {

// runtime state
struct YuvFrame {
    std::vector<uint8_t> y, u, v;
    int w=0,h=0, yStride=0,uStride=0,vStride=0, uvPixStride=0;
    int64_t ts=0;
};

constexpr int kRing = 8;
YuvFrame g_leftBuf[kRing], g_rightBuf[kRing];
int g_li=0, g_ri=0;
std::mutex g_mtx;

bool g_rectReady = false;

cv::Mat g_map1x, g_map1y, g_map2x, g_map2y, g_Q;
cv::Ptr<cv::StereoSGBM> g_sgbm;
cv::Ptr<cv::StereoMatcher> g_right;
cv::Ptr<cv::ximgproc::DisparityWLSFilter> g_wls;

std::mutex g_bufMtx;
std::condition_variable g_bufCv;
std::atomic<bool> g_run{false};
std::thread g_worker;

struct PCloud { std::vector<float> xyzrgb; };
PCloud g_pc;
std::atomic<int64_t> g_lastTsNs{0};
PointCloudUpdatedCallback g_pcCb = nullptr;

// cached intrinsics/extrinsics
CamIntrinsics g_KL{}, g_KR{};
CamExtrinsics g_XL{}, g_XR{};

PC_Config g_cfg;

static inline std::chrono::milliseconds hz_period(int hz) {
    return std::chrono::milliseconds(hz > 0 ? (1000 / hz) : 1000);
}
static std::atomic<int> g_targetHz{10};

static cv::Mat yuv420_to_bgr(const YuvFrame& f) {
    // Y plane (8UC1, WxH, stride=yStride)
    cv::Mat y(f.h, f.w, CV_8UC1, const_cast<uint8_t*>(f.y.data()), f.yStride);

    // Guards
    if (f.w <= 0 || f.h <= 0) return cv::Mat();
    if (f.uvPixStride != 1 && f.uvPixStride != 2) {
        LOGE("Unsupported UV pixel stride: %d", f.uvPixStride);
        return cv::Mat();
    }

    if (f.uvPixStride == 2) {
        // NV12 (UV) or NV21 (VU): build CV_8UC2 uv(H/2, W/2)

        bool isNV12 = g_cfg.isNV12;

        cv::Mat uv(f.h/2, f.w/2, CV_8UC2);
        for (int r = 0; r < uv.rows; ++r) {
            const uint8_t* pu = f.u.data() + r * f.uStride;
            const uint8_t* pv = f.v.data() + r * f.vStride;
            cv::Vec2b* prow = uv.ptr<cv::Vec2b>(r);
            for (int c = 0; c < uv.cols; ++c) {
                const uint8_t U = pu[c * f.uvPixStride];
                const uint8_t V = pv[c * f.uvPixStride];
                prow[c] = isNV12 ? cv::Vec2b(U, V)  // NV12: UV
                                   : cv::Vec2b(V, U); // NV21: VU
            }
        }
        cv::Mat bgr;
        const int code = isNV12 ? cv::COLOR_YUV2BGR_NV12 : cv::COLOR_YUV2BGR_NV21;
        cv::cvtColorTwoPlane(y, uv, bgr, code);
        return bgr;
    } else {
        // pixelStride==1: I420-like (planar U/V)
        cv::Mat u(f.h/2, f.w/2, CV_8UC1, const_cast<uint8_t*>(f.u.data()), f.uStride);
        cv::Mat v(f.h/2, f.w/2, CV_8UC1, const_cast<uint8_t*>(f.v.data()), f.vStride);
        // Upsample U,V to WxH then merge YUV
        cv::Mat uup, vup;
        cv::resize(u, uup, cv::Size(f.w, f.h), 0, 0, cv::INTER_LINEAR);
        cv::resize(v, vup, cv::Size(f.w, f.h), 0, 0, cv::INTER_LINEAR);
        cv::Mat yuv3; cv::merge(std::vector<cv::Mat>{y, uup, vup}, yuv3);
        cv::Mat bgr; cv::cvtColor(yuv3, bgr, cv::COLOR_YUV2BGR);
        return bgr;
    }
}

cv::Mat to_gray_clahe(const cv::Mat& bgr) {
    cv::Mat g; cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    auto clahe = cv::createCLAHE(g_cfg.cropLimit, cv::Size(g_cfg.tileGridW,g_cfg.tileGridH));
    cv::Mat out; clahe->apply(g, out); return out;
}

bool nearest_pair(int64_t& tL, YuvFrame& L, YuvFrame& R, int64_t maxDtMs=2) {
    // O(k^2) on tiny ring; fine here
    const int n = kRing;
    int bi=-1, bj=-1; int64_t best= (int64_t)1e18;
    for (int i=0;i<n;i++){
        if (g_leftBuf[i].ts==0) continue;
        for (int j=0;j<n;j++){
            if (g_rightBuf[j].ts==0) continue;
            int64_t d = llabs(g_leftBuf[i].ts - g_rightBuf[j].ts)/1000000; // ns→ms
            if (d < best) { best=d; bi=i; bj=j; }
        }
    }
    if (bi<0) return false;
    if (best > maxDtMs) return false;
    L = g_leftBuf[bi]; R = g_rightBuf[bj]; tL = L.ts;
    g_leftBuf[bi].ts = 0; g_rightBuf[bj].ts = 0; // consume
    return true;
}

void ensure_rectify() {
    if (g_rectReady) return;

    cv::Mat K1 = (cv::Mat_<double>(3,3) <<
        g_KL.fx, g_KL.skew, g_KL.cx,
        0,       g_KL.fy,   g_KL.cy,
        0,       0,         1);
    cv::Mat K2 = (cv::Mat_<double>(3,3) <<
        g_KR.fx, g_KR.skew, g_KR.cx,
        0,       g_KR.fy,   g_KR.cy,
        0,       0,         1);

    cv::Mat D1 = cv::Mat::zeros(1,5,CV_64F), D2 = cv::Mat::zeros(1,5,CV_64F);

    cv::Vec4d qR(-g_XR.qx, g_XR.qy, g_XR.qz, g_XR.qw);
    cv::Vec4d qL(-g_XL.qx, g_XL.qy, g_XL.qz, g_XL.qw);
    auto q2R = [](const cv::Vec4d& q){
        cv::Mat R; cv::Mat rvec = (cv::Mat_<double>(3,1) << 0,0,0);
        const double x=q[0],y=q[1],z=q[2],w=q[3];
        R = (cv::Mat_<double>(3,3) <<
            1-2*(y*y+z*z), 2*(x*y - z*w), 2*(x*z + y*w),
            2*(x*y + z*w), 1-2*(x*x+z*z), 2*(y*z - x*w),
            2*(x*z - y*w), 2*(y*z + x*w), 1-2*(x*x+y*y));
        return R;
    };
    cv::Mat RL = q2R(qL), RR = q2R(qR);
    cv::Mat tL = (cv::Mat_<double>(3,1) << -g_XL.tx, g_XL.ty, g_XL.tz);
    cv::Mat tR = (cv::Mat_<double>(3,1) << -g_XR.tx, g_XR.ty, g_XR.tz);

    // Right wrt Left
    cv::Mat R21 = RR * RL.t();
    cv::Mat t21 = tR - R21 * tL;

    cv::Mat R1,R2,P1,P2,Q;
    LOGI("g_cfg.imgW=%d, g_cfg.imgH=%d", g_cfg.imgW, g_cfg.imgH);
    cv::stereoRectify(K1,D1,K2,D2, cv::Size(g_cfg.imgW,g_cfg.imgH),
                      R21, t21, R1,R2,P1,P2,Q,
                      cv::CALIB_ZERO_DISPARITY, 0.0);

    cv::initUndistortRectifyMap(K1,D1,R1,P1, cv::Size(g_cfg.imgW,g_cfg.imgH), CV_32FC1, g_map1x,g_map1y);
    cv::initUndistortRectifyMap(K2,D2,R2,P2, cv::Size(g_cfg.imgW,g_cfg.imgH), CV_32FC1, g_map2x,g_map2y);
    g_Q = Q;
    g_rectReady = true;

    auto logMat = [](const char* name, const cv::Mat& m) {
        std::ostringstream oss;
        oss << name << ":\n" << m;
        LOGI("%s", oss.str().c_str());
    };

    LOGI("=== Extrinsics (camera-to-device) ===");
    LOGI("g_XL: tx=%.6f ty=%.6f tz=%.6f qx=%.6f qy=%.6f qz=%.6f qw=%.6f",
         g_XL.tx, g_XL.ty, g_XL.tz, g_XL.qx, g_XL.qy, g_XL.qz, g_XL.qw);
    LOGI("g_XR: tx=%.6f ty=%.6f tz=%.6f qx=%.6f qy=%.6f qz=%.6f qw=%.6f",
         g_XR.tx, g_XR.ty, g_XR.tz, g_XR.qx, g_XR.qy, g_XR.qz, g_XR.qw);

    logMat("RL (Left->Device rotation)", RL);
    logMat("RR (Right->Device rotation)", RR);
    logMat("tL (Left->Device translation)", tL);
    logMat("tR (Right->Device translation)", tR);

    logMat("R21 (Right->Left rotation)", R21);
    logMat("t21 (Right->Left translation)", t21);

    logMat("R1 (rectified left)", R1);
    logMat("R2 (rectified right)", R2);
    logMat("P1 (rectified left proj)", P1);
    logMat("P2 (rectified right proj)", P2);
    logMat("Q", Q);

    // SGBM
    int block = g_cfg.blockSize;
    int numDisp = g_cfg.numDisparities;
    int numCh = 1;
    g_sgbm = cv::StereoSGBM::create(
        g_cfg.minDisparity,
        (numDisp+15)/16*16,     // numDisparities
        block,                  // blockSize
        g_cfg.p1Mul*numCh*block*block,       // P1
        g_cfg.p2Mul*numCh*block*block,       // P2
        g_cfg.disp12MaxDiff,
        g_cfg.preFilterCap,
        g_cfg.uniquenessRatio,
        g_cfg.speckleWindowSize,
        g_cfg.speckleRange, 
        g_cfg.mode
    );
    g_right = cv::ximgproc::createRightMatcher(g_sgbm);
    g_wls = cv::ximgproc::createDisparityWLSFilter(g_sgbm);
    g_wls->setLambda(g_cfg.wlsLambda);
    g_wls->setSigmaColor(g_cfg.wlsSigmaColor);
}

void process_pair(const YuvFrame& L, const YuvFrame& R) {
    double scale  = g_cfg.scale;
    float confThr = g_cfg.confThr;
    float lr_tol  = g_cfg.lrTolerance;
    float zMax    = g_cfg.zMax;

    auto t0 = std::chrono::steady_clock::now();
    ensure_rectify();

    cv::Mat Lbgr = yuv420_to_bgr(L);
    cv::Mat Rbgr = yuv420_to_bgr(R);

    cv::Mat Lrect, Rrect;
    cv::remap(Lbgr, Lrect, g_map1x, g_map1y, cv::INTER_LINEAR);
    cv::remap(Rbgr, Rrect, g_map2x, g_map2y, cv::INTER_LINEAR);

    cv::Mat Ls, Rs;
    cv::resize(Lrect, Ls, cv::Size(), scale, scale, cv::INTER_AREA);
    cv::resize(Rrect, Rs, cv::Size(), scale, scale, cv::INTER_AREA);

    cv::Mat gL = to_gray_clahe(Ls);
    cv::Mat gR = to_gray_clahe(Rs);

    cv::Mat dispL_16S, dispR_16S;
    g_sgbm->compute(gL, gR, dispL_16S);
    g_right->compute(gR, gL, dispR_16S);

    cv::Mat disp_wls_16S;
    g_wls->filter(dispL_16S, gL, disp_wls_16S, dispR_16S);

    cv::Mat conf32f = g_wls->getConfidenceMap();

    cv::Mat dispS; disp_wls_16S.convertTo(dispS, CV_32F, 1.0/16.0);

    cv::Mat dL32f, dR32f;
    disp_wls_16S.convertTo(dL32f, CV_32F, 1.0/16.0);
    dispR_16S.convertTo(dR32f, CV_32F, 1.0/16.0);

    cv::Mat dispFull(Lrect.rows, Lrect.cols, CV_32F);
    const float invScale = 1.0f / static_cast<float>(scale);

    #pragma omp parallel for
    for (int Y = 0; Y < dispFull.rows; ++Y) {
        float* df = dispFull.ptr<float>(Y);
        int y = std::min((int)std::lround(Y * scale), dL32f.rows - 1); // nearest-neighbor index
        const float* lrow = dL32f.ptr<float>(y);
        const float* rrow = dR32f.ptr<float>(y);
        const float* crow = conf32f.ptr<float>(y);

        for (int X = 0; X < dispFull.cols; ++X) {
            int x = std::min((int)std::lround(X * scale), dL32f.cols - 1);

            float dl = lrow[x];
            if (!(dl > 0)) { df[X] = 0.f; continue; }
            if (crow[x] < confThr) { df[X] = 0.f; continue; }

            int xr = x - (int)std::lround(dl);                 // LR hard check (nearest)
            if ((unsigned)xr >= (unsigned)dR32f.cols) { df[X] = 0.f; continue; }
            float dr = rrow[xr];
            if (std::fabs(dl + dr) > lr_tol) { df[X] = 0.f; continue; }

            df[X] = dl * invScale; // valid: write scaled disparity to full-res
        }
    }

    cv::Mat xyz;
    cv::reprojectImageTo3D(dispFull, xyz, g_Q, true, CV_32F);

    std::vector<float> out; out.reserve(30000*6);
    int rowStep = g_cfg.outputRowStep;
    int colStep = g_cfg.outputColStep;

    const cv::Mat& rgbSrc = Lrect;
    for (int y=0;y<xyz.rows;y+=rowStep){
        const cv::Vec3f* row  = xyz.ptr<cv::Vec3f>(y);
        const float*     drow = dispFull.ptr<float>(y);
        const cv::Vec3b* crow = rgbSrc.ptr<cv::Vec3b>(y);
        for (int x=0;x<xyz.cols;x+=colStep){
            const float d = drow[x];
            if (!(d>0)) continue;
            const auto& p = row[x];
            if (p[2] > 0.0f && p[2] < zMax) {
                const cv::Vec3b cBGR = crow[x];
                out.push_back(p[0]); out.push_back(p[1]); out.push_back(p[2]);
                out.push_back(cBGR[2]*(1.0f/255.0f));
                out.push_back(cBGR[1]*(1.0f/255.0f));
                out.push_back(cBGR[0]*(1.0f/255.0f));
            }
        }
    }

    const int npts = (int)(out.size()/6);
    {
        std::lock_guard<std::mutex> lk(g_mtx);
        g_pc.xyzrgb.swap(out);
        g_lastTsNs = L.ts;
    }
    if (g_pcCb) g_pcCb((int64_t)L.ts, npts);

    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();
    LOGI("[pp] total: %.2f ms, points: %d", ms, npts);
}

} // namespace

extern "C" bool GetPointCloud(float* dst, int maxN, int* outN) {
    if (!dst || !outN) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    const int n = std::min<int>((int)(g_pc.xyzrgb.size()/6), maxN);
    if (n>0) {
        // backward-compat: copy only XYZ (discard RGB)
        for (int i=0;i<n;i++){
            const float* s = &g_pc.xyzrgb[i*6];
            float* d = &dst[i*3];
            d[0]=s[0]; d[1]=s[1]; d[2]=s[2];
        }
    }
    *outN = n;
    return true;
 }
 
bool PC_GetPointCloudXYZRGB(float* dst, int maxN, int* outN) {
    if (!dst || !outN) return false;
    std::lock_guard<std::mutex> lk(g_mtx);
    const int n = std::min<int>((int)(g_pc.xyzrgb.size()/6), maxN);
    if (n>0) memcpy(dst, g_pc.xyzrgb.data(), n*6*sizeof(float));
    *outN = n;
    return true;
}

static void OnYuv(bool isLeft, const uint8_t* y, const uint8_t* u, const uint8_t* v,
                  int w,int h,int yStride,int uStride,int vStride,int uvPixStride,int64_t tsNs)
{
    {
        std::lock_guard<std::mutex> lk(g_bufMtx);
        YuvFrame fr; fr.w=w; fr.h=h; fr.yStride=yStride; fr.uStride=uStride; fr.vStride=vStride; fr.uvPixStride=uvPixStride; fr.ts=tsNs;
        fr.y.assign(y, y + (size_t)yStride*h);
        fr.u.assign(u, u + (size_t)uStride*(h/2));
        fr.v.assign(v, v + (size_t)vStride*(h/2));
        if (isLeft) { g_leftBuf[g_li%kRing]=std::move(fr); g_li++; }
        else        { g_rightBuf[g_ri%kRing]=std::move(fr); g_ri++; }
    }
    g_bufCv.notify_one();
}

static void ProcLoop()
{
    uint64_t lastLi=0, lastRi=0;
    std::unique_lock<std::mutex> lk(g_bufMtx);

    auto period   = hz_period(g_targetHz.load(std::memory_order_relaxed));
    auto next_due = std::chrono::steady_clock::now();

    while (g_run.load(std::memory_order_relaxed)) {
        g_bufCv.wait_until(lk, next_due, [&]{
            return !g_run.load(std::memory_order_relaxed)
                   || g_li!=lastLi || g_ri!=lastRi;
        });
        if (!g_run.load(std::memory_order_relaxed)) break;

        if (std::chrono::steady_clock::now() < next_due) {
            continue;
        }

        lastLi = g_li; lastRi = g_ri;

        YuvFrame L,R; int64_t tL=0;
        if (nearest_pair(tL, L, R, g_cfg.maxPairDtMs)) {
            lk.unlock();
            process_pair(L, R);
            lk.lock();
        }

        next_due += period;
        auto now = std::chrono::steady_clock::now();
        if (now - next_due > 5 * period) {
            next_due = now; // avoid drift on overload
        }
    }
}

void PC_InitProcessing(const PC_Config* cfg) {
    g_cfg = *cfg;
    g_targetHz.store(std::max(1, cfg->targetHz), std::memory_order_relaxed);

    StereoCam_GetIntrinsics(true,  &g_KL);
    StereoCam_GetIntrinsics(false, &g_KR);
    StereoCam_GetExtrinsics(true,  &g_XL);
    StereoCam_GetExtrinsics(false, &g_XR);
    g_rectReady = false;

    StereoCam_RegisterCallback(OnYuv);

    g_run = true;
    g_worker = std::thread(ProcLoop);
}

void PC_RegisterPointCloudUpdated(PointCloudUpdatedCallback cb) {
    g_pcCb = cb;
}

void PC_StopProcessing()
{
    if (!g_run.exchange(false)) return;
    g_bufCv.notify_all();
    if (g_worker.joinable()) g_worker.join();
}