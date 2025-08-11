#nullable enable

using UnityEngine;
using UnityEngine.Rendering.Universal;

namespace StereoMatching
{
    public class PointCloudPresenter : MonoBehaviour
    {
        [SerializeField] UniversalRendererData rendererData = null!;
        [SerializeField] StereoRunner runner = null!;
        [SerializeField] Transform hmdRoot = null!;
        [SerializeField] Material pointMaterial = null!;
        [SerializeField] int pixelSize = 2;

        PointCloudFeature? feature;

        int pointCount;
        GraphicsBuffer? pointBuffer;
        Matrix4x4 pcLocalToWorld;

        void OnEnable()
        {
            if (runner != null)
                runner.PointCloudUpdated += OnPointCloudUpdated;
        }

        void OnDisable()
        {
            if (runner != null)
                runner.PointCloudUpdated -= OnPointCloudUpdated;
        }

        void Update()
        {
            if (feature == null && !rendererData.TryGetRendererFeature<PointCloudFeature>(out feature))
                return;

            feature.SetInputs(pointMaterial, pointBuffer, pcLocalToWorld, pointCount, pixelSize, 0);
        }

        void OnPointCloudUpdated(long ts, int n)
        {
            pointCount = runner.PointCount;
            pointBuffer = runner.PointBuffer;

            Vector3 lpos; Quaternion lrot;
            runner.GetLocalPose(true, out lpos, out lrot);
            pcLocalToWorld = hmdRoot.localToWorldMatrix * Matrix4x4.TRS(lpos, lrot, Vector3.one);
        }
    }
}