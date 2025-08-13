# nullable enable

using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using AOT;
using UnityEngine;

namespace StereoMatching
{
    [StructLayout(LayoutKind.Sequential)]
    public struct CamIntrinsics { public float fx, fy, cx, cy, skew; }

    [StructLayout(LayoutKind.Sequential)]
    public struct CamExtrinsics { public float tx, ty, tz, qx, qy, qz, qw; }

    public sealed class StereoRunner : MonoBehaviour
    {
        private static class Native
        {
            [StructLayout(LayoutKind.Sequential)]
            public struct PC_Config
            {
                public int imgW, imgH;
                public int isNV12;

                public int targetHz;
                public int maxPairDtMs;

                public double cropLimit;
                public int tileGridW;
                public int tileGridH;

                public int minDisparity;
                public int numDisparities;
                public int blockSize;
                public int p1Mul;
                public int p2Mul;
                public int disp12MaxDiff;
                public int preFilterCap;
                public int uniquenessRatio;
                public int speckleWindowSize;
                public int speckleRange;
                public int mode;

                public double wlsLambda;
                public double wlsSigmaColor;

                public float scale;
                public float zMax;
                public float confThr;
                public float lrTolerance;

                public int outputRowStep;
                public int outputColStep;
            }

            [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
            public delegate void FrameCb(
                [MarshalAs(UnmanagedType.I1)] bool isLeft,
                IntPtr y, IntPtr u, IntPtr v,
                int w, int h, int yStride, int uStride, int vStride, int uvPixStride,
                long tsNs);

            [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
            public delegate void PcUpdatedCb(long tsNs, int numPoints);

            [DllImport("pc_stereo_ndk")] public static extern void StereoCam_RegisterCallback(FrameCb cb);
            [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_Init(int width, int height);
            [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_Start();
            [DllImport("pc_stereo_ndk")] public static extern void StereoCam_Stop();
            [DllImport("pc_stereo_ndk")] public static extern void StereoCam_GetExtrinsics(bool isLeft, out CamExtrinsics camExtrinsics);

            [DllImport("pc_stereo_ndk")] public static extern void PC_InitProcessing(ref PC_Config config);
            [DllImport("pc_stereo_ndk")] public static extern void PC_RegisterPointCloudUpdated(PcUpdatedCb cb);
            [DllImport("pc_stereo_ndk")] public static extern bool PC_GetPointCloudXYZRGB(IntPtr dst, int maxN, out int outN);
            [DllImport("pc_stereo_ndk")] public static extern void PC_StopProcessing();
        }

        private enum Mode
        {
            SGBM = 0,
            HH = 1,
            SGBM_3WAY = 2,
            HH4 = 3,
        }
        private struct Point { public Vector3 pos; public Vector3 rgb; }

        [Header("Capture")]
        [SerializeField] private int width = 1280;
        [SerializeField] private int height = 960;
        [SerializeField] private bool isNV12 = true;
        [SerializeField] private int targetHz = 10;
        [SerializeField] private int maxPairDtMs = 4;

        [Header("CLAHE")]
        [SerializeField] private float cropLimit = 1.2f;
        [SerializeField] private int tileGridW = 8;
        [SerializeField] private int tileGridH = 8;

        [Header("SGBM")]
        [SerializeField] private int minDisparity = 0;
        [SerializeField] private int numDisparities = 96;
        [SerializeField] private int blockSize = 5;
        [SerializeField] private int p1Mul = 8;
        [SerializeField] private int p2Mul = 96;
        [SerializeField] private int disp12MaxDiff = 1;
        [SerializeField] private int preFilterCap = 15;
        [SerializeField] private int uniquenessRatio = 35;
        [SerializeField] private int speckleWindowSize = 200;
        [SerializeField] private int speckleRange = 1;
        [SerializeField] private Mode mode = Mode.SGBM_3WAY;

        [Header("WLS")]
        [SerializeField] private float wlsLambda = 15000.0f;
        [SerializeField] private float wlsSigmaColor = 0.9f;

        [Header("Post Process")]
        [SerializeField] private float scale = 0.5f;
        [SerializeField] private float zMax = 1.5f;
        [SerializeField] private float confThr = 0.0f;
        [SerializeField] private float lrTolerance = 100.0f;

        [Header("Point Cloud")]
        [SerializeField] private int outputRowStep = 2;
        [SerializeField] private int outputColStep = 2;
        [SerializeField] private int maxPoints = 500_000;

        public GraphicsBuffer? PointBuffer { get; private set; } // stride = 24 (float6)
        public int PointCount { get; private set; }
        public long Timestamp { get; private set; }

        public bool IsUpdating { get; set; } = true;

        public event Action<long, int>? PointCloudUpdated;

        private static StereoRunner? s_instance;
        private static Native.FrameCb? s_frameCb;   // prevent GC
        private static Native.PcUpdatedCb? s_pcCb;  // prevent GC

        private readonly ConcurrentQueue<(long ts, int n)> _notifyQueue = new();
        private Point[]? _points;
        private GCHandle _pointsHandle;
        private IntPtr _pointsPtr = IntPtr.Zero;

        public void GetLocalPose(bool isLeft, out Vector3 position, out Quaternion rotation)
        {
            Native.StereoCam_GetExtrinsics(isLeft, out var camExtrinsics);

            position = new Vector3(camExtrinsics.tx, camExtrinsics.ty, -camExtrinsics.tz);
            var invRot = new Quaternion(-camExtrinsics.qx, -camExtrinsics.qy, camExtrinsics.qz, camExtrinsics.qw);
            rotation = Quaternion.Inverse(invRot) * Quaternion.Euler(180, 0, 0);
        }

        private void OnEnable()
        {
            s_instance = this;

            s_frameCb = OnFrame;
            Native.StereoCam_RegisterCallback(s_frameCb);

            if (!Native.StereoCam_Init(width, height))
            {
                Debug.LogError("StereoCam_Init failed.");
                return;
            }

            var config = GetCurrentConfig();
            Native.PC_InitProcessing(ref config);

            s_pcCb = OnPcUpdated;
            Native.PC_RegisterPointCloudUpdated(s_pcCb);

            if (!Native.StereoCam_Start())
            {
                Debug.LogError("StereoCam_Start failed.");
                return;
            }

            PointBuffer = new GraphicsBuffer(GraphicsBuffer.Target.Structured, maxPoints, sizeof(float) * 6);

            _points = new Point[maxPoints];
            _pointsHandle = GCHandle.Alloc(_points, GCHandleType.Pinned); // zero-copy target
            _pointsPtr = _pointsHandle.AddrOfPinnedObject();

            PointCount = 0;
        }

        private void OnDisable()
        {
            Native.PC_StopProcessing();
            Native.StereoCam_Stop();

            if (PointBuffer != null) { PointBuffer.Dispose(); PointBuffer = null; }
            if (_pointsHandle.IsAllocated) _pointsHandle.Free();
            _pointsPtr = IntPtr.Zero;
            _points = null;
            PointCount = 0;

            if (ReferenceEquals(s_instance, this)) s_instance = null;
        }

        private void Update()
        {
            while (_notifyQueue.TryDequeue(out var ev))
            {
                if (!IsUpdating) return;

                PullLatest(ev.ts);
                PointCloudUpdated?.Invoke(ev.ts, PointCount);
            }
        }

        private void PullLatest(long ts)
        {
            if (PointBuffer == null || _pointsPtr == IntPtr.Zero) return;
            if (!Native.PC_GetPointCloudXYZRGB(_pointsPtr, maxPoints, out var n) || n <= 0) return;

            PointCount = n;
            PointBuffer.SetData(_points, 0, 0, n);
            Timestamp = ts;

            Debug.Log($"Pull Latest ({ts}): N = {PointCount}", this);
        }

        [MonoPInvokeCallback(typeof(Native.FrameCb))]
        private static void OnFrame(bool isLeft, IntPtr y, IntPtr u, IntPtr v,
                                    int w, int h, int yStride, int uStride, int vStride, int uvPixStride, long tsNs)
        {
            // No work here; processing is native-side.
        }

        [MonoPInvokeCallback(typeof(Native.PcUpdatedCb))]
        private static void OnPcUpdated(long tsNs, int numPoints)
        {
            var inst = s_instance;
            if (inst != null) inst._notifyQueue.Enqueue((tsNs, numPoints));
        }

        private Native.PC_Config GetCurrentConfig()
        {
            return new()
            {
                imgW = width,
                imgH = height,
                isNV12 = isNV12 ? 1 : 0,
                targetHz = targetHz,
                maxPairDtMs = maxPairDtMs,
                cropLimit = cropLimit,
                tileGridW = Math.Max(1, tileGridW),
                tileGridH = Math.Max(1, tileGridH),
                minDisparity = Math.Max(0, minDisparity),
                numDisparities = (numDisparities + 15) / 16 * 16,
                blockSize = blockSize,
                p1Mul = p1Mul,
                p2Mul = p2Mul,
                disp12MaxDiff = disp12MaxDiff,
                preFilterCap = preFilterCap,
                uniquenessRatio = uniquenessRatio,
                speckleWindowSize = speckleWindowSize,
                speckleRange = speckleRange,
                mode = (int)mode,
                wlsLambda = wlsLambda,
                wlsSigmaColor = wlsSigmaColor,
                scale = Mathf.Max(0.01f, scale),
                zMax = zMax,
                confThr = confThr,
                lrTolerance = lrTolerance,
                outputRowStep = Math.Max(1, outputRowStep),
                outputColStep = Math.Max(1, outputColStep),
            };
        }
    }
}
