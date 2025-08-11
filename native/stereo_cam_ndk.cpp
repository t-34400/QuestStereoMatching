#include "stereo_cam_ndk.h"
#include <string>
#include <android/log.h>
#include <android/native_window.h>
#include <camera/NdkCameraManager.h>
#include <media/NdkImageReader.h>

#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, "StereoNDK", __VA_ARGS__)
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO,  "StereoNDK", __VA_ARGS__)

// Meta vendor tags
static const uint32_t TAG_META_CAMERA_SOURCE = 0x80004d00; // int32
static const uint32_t TAG_META_POSITION      = 0x80004d01; // int32 (0=left,1=right)

static ACameraManager* g_mgr = nullptr;
static int g_w = 0, g_h = 0;
static StereoYuvCallback g_cb = nullptr;

struct CamSide {
    std::string id;
    ACameraDevice* dev = nullptr;
    AImageReader* reader = nullptr;
    ANativeWindow* window = nullptr;
    ACaptureRequest* req = nullptr;
    ACameraOutputTarget* target = nullptr;
    ACaptureSessionOutput* out = nullptr;
    ACaptureSessionOutputContainer* container = nullptr;
    ACameraCaptureSession* session = nullptr;
    bool opened = false;
    CamIntrinsics K{};
    CamExtrinsics X{};
    bool hasParams = false;
};


static CamSide g_left, g_right;
static ACaptureSessionOutputContainer* g_container = nullptr;

static void OnImageCommon(AImageReader* reader, bool isLeft) {
    AImage* img = nullptr;
    if (AImageReader_acquireLatestImage(reader, &img) != AMEDIA_OK || !img) return;

    int32_t fmt=0; AImage_getFormat(img, &fmt);
    if (fmt != AIMAGE_FORMAT_YUV_420_888) { AImage_delete(img); return; }

    int32_t w=0,h=0; AImage_getWidth(img, &w); AImage_getHeight(img, &h);
    int64_t ts=0; AImage_getTimestamp(img, &ts);

    uint8_t *y=nullptr,*u=nullptr,*v=nullptr;
    int yLen=0,uLen=0,vLen=0;
    int yStride=0,uStride=0,vStride=0, uvPixStride=0;

    AImage_getPlaneData(img, 0, &y, &yLen);
    AImage_getPlaneRowStride(img, 0, &yStride);

    AImage_getPlaneData(img, 1, &u, &uLen);
    AImage_getPlaneRowStride(img, 1, &uStride);
    AImage_getPlanePixelStride(img, 1, &uvPixStride);

    AImage_getPlaneData(img, 2, &v, &vLen);
    AImage_getPlaneRowStride(img, 2, &vStride);

    if (g_cb) g_cb(isLeft, y,u,v, w,h, yStride,uStride,vStride, uvPixStride, ts);

    AImage_delete(img);
}

static void OnLeftAvailable(void*, AImageReader* r){ OnImageCommon(r, true);  }
static void OnRightAvailable(void*,AImageReader* r){ OnImageCommon(r, false); }

static void OnDeviceDisconnected(void*, ACameraDevice*) { LOGE("Camera disconnected"); }
static void OnDeviceError(void*, ACameraDevice*, int err) { LOGE("Camera device error: %d", err); }

void StereoCam_RegisterCallback(StereoYuvCallback cb){ g_cb = cb; }

static int read_i32(const ACameraMetadata* m, uint32_t tag, int32_t* out) {
    if (!m) return -1;
    ACameraMetadata_const_entry e{};
    camera_status_t st = ACameraMetadata_getConstEntry(m, tag, &e);
    if (st != ACAMERA_OK) {
        LOGI("Tag 0x%08x not found (status=%d)", tag, st);
        return 0; // not found
    }
    if (e.count < 1) {
        LOGI("Tag 0x%08x has no data", tag);
        return -1;
    }

    if (e.type == ACAMERA_TYPE_INT32) {
        *out = e.data.i32[0];
        return 1;
    } else if (e.type == ACAMERA_TYPE_BYTE) {
        *out = static_cast<int32_t>(e.data.u8[0]);
        return 1;
    } else {
        LOGI("Tag 0x%08x type mismatch: expected INT32/BYTE, got type=%d", tag, e.type);
        return -1;
    }
}

static int read_floats(const ACameraMetadata* m, uint32_t tag, float* dst, int needCount) {
    if (!m) return -1;
    ACameraMetadata_const_entry e{};
    camera_status_t st = ACameraMetadata_getConstEntry(m, tag, &e);
    if (st != ACAMERA_OK) return 0;
    if (e.type != ACAMERA_TYPE_FLOAT || e.count < needCount) return -1;
    for (int i=0;i<needCount;i++) dst[i] = e.data.f[i];
    return 1;
}

static bool load_params_for_id(const char* camId, CamIntrinsics* K, CamExtrinsics* X) {
    if (!camId || !K || !X) return false;
    ACameraMetadata* chars = nullptr;
    if (ACameraManager_getCameraCharacteristics(g_mgr, camId, &chars) != ACAMERA_OK || !chars) {
        LOGE("getCameraCharacteristics failed for id=%s", camId);
        return false;
    }
    float intr[5] = {};
    float t[3] = {};
    float q[4] = {};
    int s1 = read_floats(chars, ACAMERA_LENS_INTRINSIC_CALIBRATION, intr, 5);
    int s2 = read_floats(chars, ACAMERA_LENS_POSE_TRANSLATION, t, 3);
    int s3 = read_floats(chars, ACAMERA_LENS_POSE_ROTATION, q, 4);
    ACameraMetadata_free(chars);

    if (s1 <= 0) { LOGE("LENS_INTRINSIC_CALIBRATION unavailable"); return false; }
    if (s2 <= 0) { LOGE("LENS_POSE_TRANSLATION unavailable"); return false; }
    if (s3 <= 0) { LOGE("LENS_POSE_ROTATION unavailable"); return false; }

    K->fx = intr[0]; K->fy = intr[1]; K->cx = intr[2]; K->cy = intr[3]; K->skew = intr[4];
    X->tx = t[0]; X->ty = t[1]; X->tz = t[2];
    X->qx = q[0]; X->qy = q[1]; X->qz = q[2]; X->qw = q[3];
    return true;
}

static bool find_left_right_ids(const ACameraIdList* ids, const char** outLeft, const char** outRight) {
    LOGI("find_left_right_ids: numCameras=%d", ids->numCameras);

    const char* leftId  = nullptr;
    const char* rightId = nullptr;

    for (int i = 0; i < ids->numCameras; ++i) {
        const char* id = ids->cameraIds[i];
        LOGI("  Checking cameraId[%d] = %s", i, id);

        ACameraMetadata* chars = nullptr;
        camera_status_t stat = ACameraManager_getCameraCharacteristics(g_mgr, id, &chars);
        if (stat != ACAMERA_OK || !chars) {
            LOGI("    Failed to get characteristics (status=%d)", stat);
            continue;
        }

        int32_t source = -1, pos = -1;
        int a = read_i32(chars, TAG_META_CAMERA_SOURCE, &source);
        int b = read_i32(chars, TAG_META_POSITION,      &pos);

        LOGI("    TAG_META_CAMERA_SOURCE: %s (%d)", (a==1 ? "OK" : "MISSING"), source);
        LOGI("    TAG_META_POSITION:      %s (%d)", (b==1 ? "OK" : "MISSING"), pos);

        if (a == 1 && b == 1 && source == 0) {
            if (pos == 0 && !leftId)  { leftId  = id; LOGI("    -> Chosen as LEFT"); }
            if (pos == 1 && !rightId) { rightId = id; LOGI("    -> Chosen as RIGHT"); }
        }

        ACameraMetadata_free(chars);
    }

    if (leftId && rightId) {
        LOGI("find_left_right_ids: SUCCESS left=%s right=%s", leftId, rightId);
        *outLeft  = leftId;
        *outRight = rightId;
        return true;
    }

    LOGI("find_left_right_ids: FAILED to find both left and right IDs");
    return false;
}

static bool open_one(CamSide& side, bool isLeft) {
    ACameraIdList* ids = nullptr;
    if (ACameraManager_getCameraIdList(g_mgr, &ids) != ACAMERA_OK || !ids) return false;

    const char* leftId=nullptr; const char* rightId=nullptr;
    if (!find_left_right_ids(ids, &leftId, &rightId)) {
        ACameraManager_deleteCameraIdList(ids);
        LOGE("Failed to resolve left/right camera IDs via vendor tags");
        return false;
    }
    side.id = isLeft ? leftId : rightId;
    ACameraManager_deleteCameraIdList(ids);

    ACameraDevice_stateCallbacks devCb{};
    devCb.context = nullptr;
    devCb.onDisconnected = OnDeviceDisconnected;
    devCb.onError = OnDeviceError;

    if (ACameraManager_openCamera(g_mgr, side.id.c_str(), &devCb, &side.dev) != ACAMERA_OK) {
        LOGE("openCamera failed: %s", side.id.c_str());
        return false;
    }

    if (!load_params_for_id(side.id.c_str(), &side.K, &side.X)) {
        LOGE("Failed to load intrinsics/extrinsics: %s", side.id.c_str());
        return false;
    }
    side.hasParams = true;

    if (AImageReader_new(g_w, g_h, AIMAGE_FORMAT_YUV_420_888, 4, &side.reader) != AMEDIA_OK) {
        LOGE("AImageReader_new failed");
        return false;
    }
    AImageReader_ImageListener lis{};
    lis.context = nullptr;
    lis.onImageAvailable = isLeft ? OnLeftAvailable : OnRightAvailable;

    if (AImageReader_setImageListener(side.reader, &lis) != AMEDIA_OK) {
        LOGE("AImageReader_setImageListener failed");
        return false;
    }
    if (AImageReader_getWindow(side.reader, &side.window) != AMEDIA_OK || !side.window) {
        LOGE("AImageReader_getWindow failed");
        return false;
    }

    if (ACameraDevice_createCaptureRequest(side.dev, TEMPLATE_RECORD, &side.req) != ACAMERA_OK) {
        LOGE("createCaptureRequest failed");
        return false;
    }
    if (ACameraOutputTarget_create(side.window, &side.target) != ACAMERA_OK) {
        LOGE("OutputTarget_create failed");
        return false;
    }
    if (ACaptureRequest_addTarget(side.req, side.target) != ACAMERA_OK) {
        LOGE("addTarget failed");
        return false;
    }

    if (ACaptureSessionOutput_create(side.window, &side.out) != ACAMERA_OK) {
        LOGE("SessionOutput_create failed (id=%s)", side.id.c_str());
        return false;
    }
    if (ACaptureSessionOutputContainer_create(&side.container) != ACAMERA_OK) {
        LOGE("OutputContainer_create failed (id=%s)", side.id.c_str());
        return false;
    }
    if (ACaptureSessionOutputContainer_add(side.container, side.out) != ACAMERA_OK) {
        LOGE("Container_add failed (id=%s)", side.id.c_str());
        return false;
    }

    side.opened = true;
    return true;
}

bool StereoCam_Init(int width, int height) {
    if (width<=0 || height<=0) { LOGE("Invalid init size"); return false; }
    g_w = width; g_h = height;

    g_mgr = ACameraManager_create();
    if (!g_mgr) { LOGE("ACameraManager_create failed"); return false; }

    if (!open_one(g_left, true))  return false;
    if (!open_one(g_right, false)) return false;

    LOGI("Init OK: left=%s right=%s", g_left.id.c_str(), g_right.id.c_str());
    return true;
}

static bool start_one(CamSide& s) {
    ACameraCaptureSession_stateCallbacks sessCb{};
    camera_status_t rc = ACameraDevice_createCaptureSession(s.dev, s.container, &sessCb, &s.session);
    if (rc != ACAMERA_OK) {
        LOGE("createCaptureSession failed (id=%s, rc=%d)", s.id.c_str(), rc);
        return false;
    }
    int seqId=0;
    rc = ACameraCaptureSession_setRepeatingRequest(s.session, nullptr, 1, &s.req, &seqId);
    if (rc != ACAMERA_OK) {
        LOGE("setRepeatingRequest failed (id=%s, rc=%d)", s.id.c_str(), rc);
        return false;
    }
    return true;
}

bool StereoCam_Start() {
    if (!g_left.opened || !g_right.opened) { LOGE("Devices not opened"); return false; }
    if (!start_one(g_left))  return false;
    if (!start_one(g_right)) return false;
    LOGI("Capture started");
    return true;
}

void StereoCam_Stop() {
    auto stop_side = [](CamSide& s){
        if (s.session) { ACameraCaptureSession_stopRepeating(s.session); ACameraCaptureSession_close(s.session); s.session=nullptr; }
        if (s.req)     { ACaptureRequest_free(s.req); s.req=nullptr; }
        if (s.target)  { ACameraOutputTarget_free(s.target); s.target=nullptr; }
        if (s.out)     { ACaptureSessionOutput_free(s.out); s.out=nullptr; }
        if (s.container){ ACaptureSessionOutputContainer_free(s.container); s.container=nullptr; } // ← 追加
        if (s.reader)  { AImageReader_delete(s.reader); s.reader=nullptr; }
        s.window=nullptr;
        s.opened=false;
    };
    stop_side(g_left);
    stop_side(g_right);

    if (g_container) { ACaptureSessionOutputContainer_free(g_container); g_container=nullptr; }
    if (g_mgr) { ACameraManager_delete(g_mgr); g_mgr=nullptr; }
    LOGI("Capture stopped");
}

void StereoCam_Shutdown(){ StereoCam_Stop(); }

bool StereoCam_GetCameraIds(const char** outLeftId, const char** outRightId) {
    if (!g_left.opened || !g_right.opened) return false;
    if (outLeftId)  *outLeftId  = g_left.id.c_str();
    if (outRightId) *outRightId = g_right.id.c_str();
    return true;
}

bool StereoCam_GetIntrinsics(bool isLeft, CamIntrinsics* out) {
    CamSide& s = isLeft ? g_left : g_right;
    if (!s.hasParams || !out) return false;
    *out = s.K;
    return true;
}

bool StereoCam_GetExtrinsics(bool isLeft, CamExtrinsics* out) {
    CamSide& s = isLeft ? g_left : g_right;
    if (!s.hasParams || !out) return false;
    *out = s.X;
    return true;
}
