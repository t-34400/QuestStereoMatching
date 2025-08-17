# nullable enable

using System;
using System.Collections.Concurrent;
using System.Runtime.InteropServices;
using AOT;
using UnityEngine;

namespace StereoMatching
{
    public sealed class StereoRunner : MonoBehaviour
    {
        private struct Point { public Vector3 pos; public Vector3 rgb; }

        [Header("Capture")]
        [SerializeField] private int width = 1280;
        [SerializeField] private int height = 960;
        [SerializeField] private bool isNV12 = true;
        [SerializeField] private int targetHz = 10;
        [SerializeField] private int maxPairDtMs = 4;

        [Header("Point Cloud")]
        [SerializeField] private Backend backend = Backend.SGBM;
        [SerializeField] private float relGradSigma = 1.0f;
        [SerializeField] private float relGradThr = 0.0f;
        [SerializeField] private float relGradQuantile = 0.9f;
        [SerializeField] private float gradAbsCap = 32.0f;
        [SerializeField] private float zMin = 0.4f;
        [SerializeField] private float zMax = 1.5f;
        [SerializeField] private int outputRowStep = 2;
        [SerializeField] private int outputColStep = 2;
        [SerializeField] private int maxPoints = 500_000;

        [SerializeField] private SGBMConfigAsset sgbmConfigAsset = null!;
        [SerializeField] private BANetConfigAsset baNetConfigAsset = null!;

        public GraphicsBuffer? PointBuffer { get; private set; } // stride = 24 (float6)
        public int PointCount { get; private set; }
        public long Timestamp { get; private set; }

        public bool IsUpdating { get; set; } = true;

        public event Action<long, int>? PointCloudUpdated;

        private Native.PC_Config pcConfig;
        private Native.SGBM_Config sgbmConfig;
        private Native.BANet_Config baNetConfig;

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

        private async void Awake()
        {
            pcConfig = GetPCConfig();
            sgbmConfig = sgbmConfigAsset.ToNativeConfig();
            baNetConfig = await baNetConfigAsset.ToNativeConfigAsync();
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

            Native.PC_InitProcessing(
                ref pcConfig, ref sgbmConfig, ref baNetConfig
            );

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

        private Native.PC_Config GetPCConfig()
        {
            return new()
            {
                imgW = width,
                imgH = height,
                isNV12 = isNV12 ? 1 : 0,

                targetHz = targetHz,
                maxPairDtMs = maxPairDtMs,

                backend = (int) backend,
                relGradSigma = relGradSigma,
                relGradThr = relGradThr,
                relGradQuantile = relGradQuantile,
                gradAbsCap = gradAbsCap,
                zMin = zMin,
                zMax = zMax,
                outputRowStep = Math.Max(1, outputRowStep),
                outputColStep = Math.Max(1, outputColStep),
            };
        }

        private enum Backend
        {
            SGBM = 0,
            BANet = 1,
        }
    }
}
