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

void  StereoCam_RegisterCallback(StereoYuvCallback cb);
bool  StereoCam_Init(int width, int height);
bool  StereoCam_Start();
void  StereoCam_Stop();
void  StereoCam_Shutdown();

bool  StereoCam_GetCameraIds(const char** outLeftId, const char** outRightId);
bool  StereoCam_GetIntrinsics(bool isLeft, CamIntrinsics* out);
bool  StereoCam_GetExtrinsics(bool isLeft, CamExtrinsics* out);

void  PC_RegisterPointCloudUpdated(PointCloudUpdatedCallback cb);
bool  PC_GetPointCloudXYZRGB(float* dst, int maxN, int* outN);
} // extern "C"
