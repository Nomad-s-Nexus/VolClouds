#pragma once

// TODO-verify axis/convention assumptions at runtime before enabling by default.

namespace RadClouds::Shaders
{
    inline constexpr char kFullscreenVS[] = R"(
cbuffer CloudCB : register(b0)
{
    row_major float4x4 InvViewProj;
    float4 CameraPos;
    float4 SunDir;
    float4 SunColor;
    float4 AmbientColor;
    float4 WindAndTime;
    float4 Params0;
    float4 Params1;
    float4 Params2;
};

struct VSOut
{
    float4 pos : SV_Position;
    float2 ndc : TEXCOORD0;
};

VSOut main(uint id : SV_VertexID)
{
    VSOut o;
    float2 uv = float2((id == 2) ? 3.0 : -1.0, (id == 1) ? 3.0 : -1.0);
    o.pos = float4(uv, Params2.x, 1.0);
    o.ndc = uv;
    return o;
}
)";

    inline constexpr char kCloudPS[] = R"(
cbuffer CloudCB : register(b0)
{
    row_major float4x4 InvViewProj;
    float4 CameraPos;
    float4 SunDir;
    float4 SunColor;
    float4 AmbientColor;
    float4 WindAndTime;
    float4 Params0;
    float4 Params1;
    float4 Params2;
};

float Hash13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float Noise3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float n000 = Hash13(i + float3(0, 0, 0));
    float n100 = Hash13(i + float3(1, 0, 0));
    float n010 = Hash13(i + float3(0, 1, 0));
    float n110 = Hash13(i + float3(1, 1, 0));
    float n001 = Hash13(i + float3(0, 0, 1));
    float n101 = Hash13(i + float3(1, 0, 1));
    float n011 = Hash13(i + float3(0, 1, 1));
    float n111 = Hash13(i + float3(1, 1, 1));
    float nx00 = lerp(n000, n100, f.x);
    float nx10 = lerp(n010, n110, f.x);
    float nx01 = lerp(n001, n101, f.x);
    float nx11 = lerp(n011, n111, f.x);
    return lerp(lerp(nx00, nx10, f.y), lerp(nx01, nx11, f.y), f.z);
}

float Fbm(float3 p)
{
    return Noise3(p) * 0.65 + Noise3(p * 2.03 + 17.1) * 0.35;
}

float DensityAt(float3 p, float bottom, float top)
{
    float height01 = saturate((p.z - bottom) / max(top - bottom, 1.0));
    if (height01 <= 0.0 || height01 >= 1.0)
        return 0.0;

    float2 wind = float2(cos(WindAndTime.x), sin(WindAndTime.x)) * WindAndTime.y * 0.0025;
    float3 q = p * (0.00085 * Params1.y) + float3(wind.x * WindAndTime.z, wind.y * WindAndTime.z, 0.0);

    float n = Fbm(q);
    float profile = smoothstep(0.0, 0.22, height01) * (1.0 - smoothstep(0.62, 1.0, height01));
    float coverage = saturate(WindAndTime.w);
    float threshold = lerp(0.78, 0.30, coverage);
    float d = max(0.0, n - threshold) / max(1.0 - threshold, 1e-4);
    return d * profile * Params0.z;
}

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    if (Params2.w > 0.5)
        return float4(0.8, 0.0, 0.8, 0.6);
    float4 nearH = mul(float4(ndc, Params2.y, 1.0), InvViewProj);
    float4 farH = mul(float4(ndc, Params2.z, 1.0), InvViewProj);
    if (abs(nearH.w) < 1e-6 || abs(farH.w) < 1e-6)
        discard;
    float3 nearP = nearH.xyz / nearH.w;
    float3 farP = farH.xyz / farH.w;
    float3 dir = farP - nearP;
    float rayLen = length(dir);
    if (rayLen < 1e-4)
        discard;
    dir /= rayLen;

    float3 origin = CameraPos.xyz;
    float bottom = Params0.x;
    float top = Params0.y;

    float t0 = 0.0;
    float t1 = min(rayLen, Params1.x);
    if (abs(dir.z) < 1e-6)
    {
        if (origin.z < bottom || origin.z > top)
            discard;
    }
    else
    {
        float ta = (bottom - origin.z) / dir.z;
        float tb = (top - origin.z) / dir.z;
        t0 = max(t0, min(ta, tb));
        t1 = min(t1, max(ta, tb));
    }
    if (t1 <= t0)
        discard;

    int steps = (int)clamp(Params0.w, 4.0, 64.0);
    float stepLen = (t1 - t0) / (float)steps;

    float transmittance = 1.0;
    float3 scattered = float3(0.0, 0.0, 0.0);
    float3 sunDir = normalize(SunDir.xyz);
    float cosSun = saturate(dot(dir, sunDir));

    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float t = t0 + stepLen * ((float)i + 0.5);
        float3 p = origin + dir * t;
        float density = DensityAt(p, bottom, top);
        if (density > 0.0)
        {
            float3 towardSun = p + sunDir * 250.0;
            float sunDensity = DensityAt(towardSun, bottom, top);
            float sunTerm = exp(-sunDensity * 3.0);
            float phase = 0.55 + 0.45 * pow(cosSun, 8.0);
            float3 lightCol = AmbientColor.rgb + SunColor.rgb * (SunDir.w * sunTerm * phase);
            float extinction = exp(-density * stepLen);
            float alpha = 1.0 - extinction;
            scattered += transmittance * alpha * lightCol;
            transmittance *= extinction;
            if (transmittance < 0.02)
                break;
        }
    }

    float alpha = saturate((1.0 - transmittance) * Params1.w);
    if (alpha <= 0.001)
        discard;

    float flash = Params1.z;
    float3 color = scattered + AmbientColor.rgb * flash * 0.25;
    return float4(color, alpha);
}
)";
}
