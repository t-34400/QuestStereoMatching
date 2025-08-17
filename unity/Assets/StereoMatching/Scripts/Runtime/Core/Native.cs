# nullable enable

using System;
using System.Runtime.InteropServices;

namespace StereoMatching
{
    static class Native
    {
        [StructLayout(LayoutKind.Sequential)]
        public struct CamIntrinsics { public float fx, fy, cx, cy, skew; }

        [StructLayout(LayoutKind.Sequential)]
        public struct CamExtrinsics { public float tx, ty, tz, qx, qy, qz, qw; }

        [StructLayout(LayoutKind.Sequential)]
        public struct PC_Config
        {
            public int imgW, imgH;
            public int isNV12;

            public int targetHz;
            public int maxPairDtMs;

            public int backend;

            public float relGradSigma;
            public float relGradThr;
            public float relGradQuantile;
            public float gradAbsCap;
            public float zMin;
            public float zMax;
            public int outputRowStep;
            public int outputColStep;
        }

        [StructLayout(LayoutKind.Sequential)]
        public struct SGBM_Config
        {
            public float scale;

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

            public float confThr;
            public float lrTolerance;
        }

        [StructLayout(LayoutKind.Sequential, CharSet = CharSet.Ansi)]
        public struct BANet_Config
        {
            public int inputWidth;
            public int inputHeight;
            [MarshalAs(UnmanagedType.ByValTStr, SizeConst = 256)]
            public string modelPath;
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

        [DllImport("pc_stereo_ndk")] public static extern void PC_InitProcessing(ref PC_Config config, ref SGBM_Config sgbmConfig, ref BANet_Config baNetConfig);
        [DllImport("pc_stereo_ndk")] public static extern void PC_RegisterPointCloudUpdated(PcUpdatedCb cb);
        [DllImport("pc_stereo_ndk")] public static extern bool PC_GetPointCloudXYZRGB(IntPtr dst, int maxN, out int outN);
        [DllImport("pc_stereo_ndk")] public static extern void PC_StopProcessing();
    }
}