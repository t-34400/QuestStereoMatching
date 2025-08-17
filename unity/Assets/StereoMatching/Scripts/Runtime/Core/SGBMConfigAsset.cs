# nullable enable

using System;
using UnityEngine;

namespace StereoMatching
{
    [CreateAssetMenu(menuName = "StereoMatching/Config/SGBM", fileName = "SGBMConfigAsset")]
    sealed class SGBMConfigAsset : ScriptableObject
    {
        [Header("CLAHE")]
        [SerializeField] private float cropLimit = 1.2f;
        [SerializeField] private int tileGridW = 8;
        [SerializeField] private int tileGridH = 8;

        [Header("SGBM")]
        [SerializeField] private float scale = 0.5f;
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
        [SerializeField] private float confThr = 0.0f;
        [SerializeField] private float lrTolerance = 100.0f;

        public Native.SGBM_Config ToNativeConfig()
        {
            return new Native.SGBM_Config()
            {
                scale = Mathf.Max(0.01f, scale),

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

                confThr = confThr,
                lrTolerance = lrTolerance,
            };
        }

        private enum Mode
        {
            SGBM = 0,
            HH = 1,
            SGBM_3WAY = 2,
            HH4 = 3,
        }
    }
}