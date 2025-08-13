Shader "PointCloud/Billboard"
{
    SubShader
    {
        Tags { "Queue"="Transparent" "RenderType"="Transparent" }
        Pass
        {
            Cull Off
            ZTest LEqual
            Blend Off

            HLSLPROGRAM
            #pragma target 4.5
            #pragma vertex Vert
            #pragma fragment Frag
            #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

            struct Point { float3 pos; float3 rgb; };
            StructuredBuffer<Point> _PointBuffer;
            float4x4 _PointCloudLocalToWorld;
            int _PixelSize;

            struct VS_OUT { float4 clipPos:SV_Position; float3 rgb:TEXCOORD0; };

            static const uint LUT[6] = {0,1,2, 0,2,3};

            VS_OUT Vert(uint vid:SV_VertexID)
            {
                uint i  = vid / 6;
                uint ci = LUT[vid % 6];

                Point p = _PointBuffer[i];

                p.pos.y *= -1;

                float4 w = mul(_PointCloudLocalToWorld, float4(p.pos, 1));
                float4 c = mul(unity_MatrixVP, w);
                if (abs(c.w) < 1e-6) c.w = 1e-6;

                float s = (float)max(1, _PixelSize);
                float2 corners[4] = {
                    float2(-s,-s), float2(s,-s), float2(s,s), float2(-s,s)
                };
                float2 ndc = (c.xy / c.w) + (corners[ci] * (2.0 / _ScreenParams.xy));

                VS_OUT o;
                o.clipPos = float4(ndc * c.w, c.z, c.w);
                o.rgb = p.rgb;
                return o;
            }

            float4 Frag(VS_OUT i):SV_Target
            {
                return float4(i.rgb, 1.0);
            }
            ENDHLSL
        }
    }
}
