Shader "Unlit/PointCloud_Billboard"
{
    SubShader
    { Pass
      {
        ZWrite On ZTest LEqual Cull Off
        HLSLPROGRAM
        #pragma vertex vert
        #pragma fragment frag
        #pragma multi_compile_instancing
        #include "Packages/com.unity.render-pipelines.universal/ShaderLibrary/Core.hlsl"

        StructuredBuffer<float3> _XYZRGB;
        float4x4 _MRectLeftToWorld;
        float _Fx;
        float _PointSizePx;

        struct appdata {
          float3 posOS : POSITION;
          float2 uv    : TEXCOORD0;
          UNITY_VERTEX_INPUT_INSTANCE_ID
        };
        struct v2f {
          float4 posCS : SV_Position;
          float3 col   : COLOR0;
        };

        v2f vert(appdata v, uint instID : SV_InstanceID)
        {
            UNITY_SETUP_INSTANCE_ID(v);
            v2f o;

            uint iPos = instID * 2u;
            uint iRgb = iPos + 1u;
            float3 p_rect = _XYZRGB[iPos];
            float3 rgb    = _XYZRGB[iRgb];

            // rect-left -> world -> view
            float4 wpos = mul(_MRectLeftToWorld, float4(p_rect,1));
            float4 vpos = mul(UNITY_MATRIX_V, wpos);

            // pixel-constant size (approx): size_world = sizePx * (-z_view) / fx_px
            float scale = _PointSizePx * (-vpos.z) / max(_Fx, 1e-6);

            // billboard in view space (X=right, Y=up)
            float2 o2 = v.uv;           // [-1,+1]
            float4 vCorner = vpos + float4(o2.x * scale, o2.y * scale, 0, 0);

            o.posCS = mul(UNITY_MATRIX_P, vCorner);
            o.col   = rgb;
            return o;
        }

        half4 frag(v2f i) : SV_Target { return half4(i.col, 1); }
        ENDHLSL
      } }
}
