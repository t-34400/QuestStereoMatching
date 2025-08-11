using System;
using System.Runtime.InteropServices;
using UnityEngine;
using AOT;

namespace StereoMatching
{
    [StructLayout(LayoutKind.Sequential)]
    public struct CamIntrinsics { public float fx, fy, cx, cy, skew; }
    [StructLayout(LayoutKind.Sequential)]
    public struct CamExtrinsics { public float tx, ty, tz, qx, qy, qz, qw; }

    static class StereoNDK
    {
        [UnmanagedFunctionPointer(CallingConvention.Cdecl)]
        public delegate void FrameCb(
            [MarshalAs(UnmanagedType.I1)] bool isLeft,
            IntPtr y, IntPtr u, IntPtr v,
            int w, int h, int yStride, int uStride, int vStride, int uvPixStride,
            long tsNs);

        [DllImport("pc_stereo_ndk")] public static extern void StereoCam_RegisterCallback(FrameCb cb);
        [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_Init(int width, int height);
        [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_Start();
        [DllImport("pc_stereo_ndk")] public static extern void StereoCam_Stop();
        [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_GetCameraIds(out IntPtr leftId, out IntPtr rightId);
        [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_GetIntrinsics([MarshalAs(UnmanagedType.I1)] bool isLeft, out CamIntrinsics k);
        [DllImport("pc_stereo_ndk")] public static extern bool StereoCam_GetExtrinsics([MarshalAs(UnmanagedType.I1)] bool isLeft, out CamExtrinsics x);
    }

    public class StereoRunner : MonoBehaviour
    {
        static StereoNDK.FrameCb _cb; // prevent GC

        void OnEnable()
        {
            _cb = OnFrame; // keep delegate alive
            StereoNDK.StereoCam_RegisterCallback(_cb);

            if (!StereoNDK.StereoCam_Init(1280, 720))
            {
                Debug.LogError("StereoCam_Init failed.");
                return;
            }

            if (StereoNDK.StereoCam_GetCameraIds(out var l, out var r))
                Debug.Log($"IDs: L={Marshal.PtrToStringAnsi(l)} R={Marshal.PtrToStringAnsi(r)}");

            if (StereoNDK.StereoCam_GetIntrinsics(true, out var Kl) &&
                StereoNDK.StereoCam_GetExtrinsics(true, out var Xl))
                Debug.Log($"Left K: fx={Kl.fx} fy={Kl.fy} cx={Kl.cx} cy={Kl.cy} skew={Kl.skew} / T=({Xl.tx},{Xl.ty},{Xl.tz}) q=({Xl.qx},{Xl.qy},{Xl.qz},{Xl.qw})");

            if (StereoNDK.StereoCam_GetIntrinsics(false, out var Kr) &&
                StereoNDK.StereoCam_GetExtrinsics(false, out var Xr))
                Debug.Log($"Right K: fx={Kr.fx} fy={Kr.fy} cx={Kr.cx} cy={Kr.cy} skew={Kr.skew} / T=({Xr.tx},{Xr.ty},{Xr.tz}) q=({Xr.qx},{Xr.qy},{Xr.qz},{Xr.qw})");

            if (!StereoNDK.StereoCam_Start())
                Debug.LogError("StereoCam_Start failed.");
        }

        void OnDisable()
        {
            StereoNDK.StereoCam_Stop();
        }

        [MonoPInvokeCallback(typeof(StereoNDK.FrameCb))]
        static void OnFrame(bool isLeft, IntPtr y, IntPtr u, IntPtr v,
                            int w, int h, int yStride, int uStride, int vStride, int uvPixStride, long tsNs)
        {
            // Use Y-plane pointer directly (no copies). Forward to native processing if needed.
        }
    }
}
