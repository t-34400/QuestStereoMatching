#pragma once
#include <cstdint>

extern "C" {

typedef void (*StereoYuvCallback)(
    bool isLeft,
    const uint8_t* y, const uint8_t* u, const uint8_t* v,
    int width, int height,
    int yRowStride, int uRowStride, int vRowStride,
    int uvPixelStride,
    int64_t timestampNs);
typedef void (*PointCloudUpdatedCallback)(int64_t timestampNs, int numPoints);

struct CamIntrinsics { float fx, fy, cx, cy, skew; };

struct CamExtrinsics {
    float tx, ty, tz;
    float qx, qy, qz, qw;
};

struct PC_Config {
    int imgW, imgH;
    int isNV12;              // 1: NV12 (UV), 0: NV21 (VU)

    int targetHz;            // processing loop Hz
    int maxPairDtMs;         // L/R pairing window

    int backend;             // 0: SGBM, 1: BANet

    float relGradSigma;
    float relGradThr;
    float relGradQuantile;
    float gradAbsCap;
    float zMin;
    float zMax;
    int outputRowStep;
    int outputColStep;
};

struct SGBM_Config {
    float scale;             // 0<scale<=1, stereo at reduced scale

    // CLAHE
    double cropLimit;
    int tileGridW;
    int tileGridH;

    // SGBM
    int minDisparity;
    int numDisparities;      // multiple of 16
    int blockSize;
    int p1Mul;
    int p2Mul; 
    int disp12MaxDiff;
    int preFilterCap;
    int uniquenessRatio;
    int speckleWindowSize;
    int speckleRange;
    int mode;                // 0:SGBM 1:HH 2:SGBM_3WAY 3:HH4

    // WLS filter
    double wlsLambda;
    double wlsSigmaColor;

    // Post / reprojection
    float confThr;           // confidence threshold
    float lrTolerance;       // LR check tolerance
};

struct BANet_Config {
    int inputW, inputH;
    char modelPath[256];
};

void  StereoCam_RegisterCallback(StereoYuvCallback cb);
bool  StereoCam_Init(int width, int height);
bool  StereoCam_Start();
void  StereoCam_Stop();
void  StereoCam_Shutdown();

bool  StereoCam_GetCameraIds(const char** outLeftId, const char** outRightId);
bool  StereoCam_GetIntrinsics(bool isLeft, CamIntrinsics* out);
bool  StereoCam_GetExtrinsics(bool isLeft, CamExtrinsics* out);

void  PC_InitProcessing(const PC_Config* cfg, const SGBM_Config* sgbmCfg, const BANet_Config* banetCfg);
void  PC_RegisterPointCloudUpdated(PointCloudUpdatedCallback cb);
bool  PC_GetPointCloudXYZRGB(float* dst, int maxN, int* outN);
void  PC_StopProcessing();
} // extern "C"

enum class BackendType : int {
    SGBM = 0,
    ExecuTorch = 1,
};