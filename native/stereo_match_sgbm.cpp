#include "stereo_match_sgbm.h"

#include <opencv2/core.hpp>
#include <opencv2/calib3d.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/ximgproc/disparity_filter.hpp>

cv::Ptr<cv::StereoSGBM> g_sgbm;
cv::Ptr<cv::StereoMatcher> g_right;
cv::Ptr<cv::ximgproc::DisparityWLSFilter> g_wls;

SGBM_Config g_sgbmCfg;

cv::Mat to_gray_clahe(const cv::Mat& bgr) {
    cv::Mat g; cv::cvtColor(bgr, g, cv::COLOR_BGR2GRAY);
    auto clahe = cv::createCLAHE(g_sgbmCfg.cropLimit, cv::Size(g_sgbmCfg.tileGridW,g_sgbmCfg.tileGridH));
    cv::Mat out; clahe->apply(g, out); return out;
}

void init_stereo_sgbm(const SGBM_Config* sgbmCfg) {
    g_sgbmCfg = *sgbmCfg;

    int block = g_sgbmCfg.blockSize;
    int numDisp = g_sgbmCfg.numDisparities;
    int numCh = 1;
    g_sgbm = cv::StereoSGBM::create(
        g_sgbmCfg.minDisparity,
        (numDisp+15)/16*16,     // numDisparities
        block,                  // blockSize
        g_sgbmCfg.p1Mul*numCh*block*block,       // P1
        g_sgbmCfg.p2Mul*numCh*block*block,       // P2
        g_sgbmCfg.disp12MaxDiff,
        g_sgbmCfg.preFilterCap,
        g_sgbmCfg.uniquenessRatio,
        g_sgbmCfg.speckleWindowSize,
        g_sgbmCfg.speckleRange, 
        g_sgbmCfg.mode
    );
    g_right = cv::ximgproc::createRightMatcher(g_sgbm);
    g_wls = cv::ximgproc::createDisparityWLSFilter(g_sgbm);
    g_wls->setLambda(g_sgbmCfg.wlsLambda);
    g_wls->setSigmaColor(g_sgbmCfg.wlsSigmaColor);
}

cv::Mat estimate_disparity_sgbm(cv::Mat Lrect, cv::Mat Rrect) {
    double scale  = g_sgbmCfg.scale;
    float confThr = g_sgbmCfg.confThr;
    float lr_tol  = g_sgbmCfg.lrTolerance;

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

    return dispFull;
}