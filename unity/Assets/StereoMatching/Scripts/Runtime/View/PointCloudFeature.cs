using UnityEngine;
using UnityEngine.Rendering;
using UnityEngine.Rendering.Universal;
using UnityEngine.Rendering.RenderGraphModule;

namespace StereoMatching
{
    [SupportedOnRenderer(typeof(UniversalRendererData))]
    public class PointCloudFeature : ScriptableRendererFeature
    {
        class PcPass : ScriptableRenderPass
        {
            public Material mat;
            public GraphicsBuffer buf;
            public Matrix4x4 l2w;
            public int count, pixelSize, passIndex;

            public PcPass()
            {
                renderPassEvent = RenderPassEvent.AfterRenderingTransparents;
            }

            private class PassData
            {
                public Material mat;
                public GraphicsBuffer buf;
                public Matrix4x4 l2w;
                public int count, pixelSize, passIndex;
            }

            public override void RecordRenderGraph(RenderGraph rg, ContextContainer frameData)
            {
                var urpRes  = frameData.Get<UniversalResourceData>();
                var camData = frameData.Get<UniversalCameraData>();

                using var builder = rg.AddRasterRenderPass<PassData>("PC Draw (RG)", out var data);

                data.mat = mat; data.buf = buf; data.l2w = l2w;
                data.count = count; data.pixelSize = pixelSize; data.passIndex = passIndex;

                builder.SetRenderAttachment(urpRes.activeColorTexture, 0);
                builder.SetRenderAttachmentDepth(urpRes.activeDepthTexture, 0);
                builder.EnableFoveatedRasterization(true);
                builder.AllowPassCulling(false);

                builder.SetRenderFunc((PassData d, RasterGraphContext ctx) =>
                {
                    if (d.mat == null || d.buf == null || d.count <= 0) return;

                    d.mat.SetBuffer("_PointBuffer", d.buf);
                    d.mat.SetMatrix("_PointCloudLocalToWorld", d.l2w);
                    d.mat.SetInt("_PixelSize", Mathf.Max(1, d.pixelSize));

                    ctx.cmd.DrawProcedural(Matrix4x4.identity, d.mat, d.passIndex,
                                        MeshTopology.Triangles, d.count * 6, 1);
                });
            }
        }

        PcPass pass = null!;
        public override void Create() => pass = new PcPass();

        public override void AddRenderPasses(ScriptableRenderer renderer, ref RenderingData data)
        {
            renderer.EnqueuePass(pass);
        }

        // expose a setter for your presenter
        public void SetInputs(Material m, GraphicsBuffer b, Matrix4x4 l2w, int count, int px, int passIdx)
        {
            if (pass == null) return;

            pass.mat = m;
            pass.buf = b;
            pass.l2w = l2w;
            pass.count = count;
            pass.pixelSize = px;
            pass.passIndex = passIdx;
        }
    }
}