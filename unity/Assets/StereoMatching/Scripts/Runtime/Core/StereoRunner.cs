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
            public struct CamExtrinsics { public float tx, ty, tz, qx, qy, qz, qw; }

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

            [DllImport("pc_stereo_ndk")] public static extern void PC_InitProcessing(int imgW, int imgH);
            [DllImport("pc_stereo_ndk")] public static extern void PC_RegisterPointCloudUpdated(PcUpdatedCb cb);
            [DllImport("pc_stereo_ndk")] public static extern bool PC_GetPointCloudXYZRGB(IntPtr dst, int maxN, out int outN);
            [DllImport("pc_stereo_ndk")] public static extern void PC_StopProcessing();
        }

        private struct Point { public Vector3 pos; public Vector3 rgb; }

        [Header("Capture")]
        [SerializeField] private int width = 1280;
        [SerializeField] private int height = 960;

        [Header("Point Cloud")]
        [SerializeField] private int maxPoints = 500_000;

        public GraphicsBuffer? PointBuffer { get; private set; } // stride = 24 (float6)
        public int PointCount { get; private set; }
        public long Timestamp { get; private set; }

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

            Native.PC_InitProcessing(width, height);

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
    }
}
