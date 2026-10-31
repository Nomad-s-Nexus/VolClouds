#pragma once

// TODO-verify cloud look constants in game before treating them as final art direction.

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
    float4 Params3;
    float4 Params4;
    float4 Params5;
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
    float4 Params0;   // low layer: bottom, top, density, steps
    float4 Params1;   // max distance, noise scale, flash, overall alpha
    float4 Params2;   // far z, near z, far z, debug mode
    float4 Params3;   // high layer: bottom, top, density, coverage
    float4 Params4;   // phase g, powder, ambient boost, horizon fade
    float4 Params5;   // storm darken, storm coverage, cirrus, low coverage
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

float Fbm(float3 p, int octaves)
{
    float sum = 0.0;
    float amp = 0.55;
    float2x2 rot = float2x2(0.80, -0.60, 0.60, 0.80);
    float3 q = p;
    for (int i = 0; i < octaves; ++i)
    {
        sum += Noise3(q) * amp;
        q = float3(mul(rot, q.xz), q.y * 1.6) * 2.02 + 11.7;
        amp *= 0.5;
    }
    return sum;
}

float Billow(float3 p)
{
    float n = Fbm(p, 4);
    return 1.0 - abs(n * 2.0 - 1.0);
}

float LayerDensity(float3 p, float bottom, float top, float coverage, float scale, float windSign)
{
    float height01 = saturate((p.z - bottom) / max(top - bottom, 1.0));
    if (height01 <= 0.0 || height01 >= 1.0)
        return 0.0;

    float2 windDir = float2(cos(WindAndTime.x), sin(WindAndTime.x)) * WindAndTime.y * 0.0025 * windSign;
    float3 wind = float3(windDir.x, windDir.y, 0.0) * WindAndTime.z;
    float3 q = p * (0.00060 * Params1.y * scale) + wind;

    float warp = Fbm(q * 0.55 + 3.1, 2);
    q += float3(warp, warp * 0.6, warp * 0.3) * 1.8;

    float billow = Billow(q);
    float detail = Fbm(q * 3.4 + 21.0, 2);
    float n = billow * 0.78 + detail * 0.22;

    float threshold = lerp(0.72, 0.28, saturate(coverage));
    float d = max(0.0, n - threshold) / max(1.0 - threshold, 1e-4);
    d = saturate(d) * saturate(d + 0.25);

    float verticalProfile = smoothstep(0.0, 0.18, height01) * (1.0 - smoothstep(0.55, 1.0, height01));
    return d * verticalProfile;
}

float HenyeyGreenstein(float cosTheta, float g)
{
    float g2 = g * g;
    return (1.0 - g2) / (4.0 * 3.14159265 * pow(max(1.0 + g2 - 2.0 * g * cosTheta, 1e-4), 1.5));
}

float SampleDensity(float3 p)
{
    float low = LayerDensity(p, Params0.x, Params0.y, Params5.w, 1.0, 1.0) * Params0.z;
    float high = LayerDensity(p, Params3.x, Params3.y, Params3.w, 2.6, 0.55) * Params3.z * Params5.z;
    return low + high;
}

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    if (Params2.w > 0.5 && Params2.w < 1.5)
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

    float slabBottom = min(Params0.x, Params3.x);
    float slabTop = max(Params0.y, Params3.y);
    float3 origin = CameraPos.xyz;
    float t0 = 0.0;
    float t1 = min(rayLen, Params1.x);
    if (abs(dir.z) < 1e-6)
    {
        if (origin.z < slabBottom || origin.z > slabTop)
            discard;
    }
    else
    {
        float ta = (slabBottom - origin.z) / dir.z;
        float tb = (slabTop - origin.z) / dir.z;
        t0 = max(t0, min(ta, tb));
        t1 = min(t1, max(ta, tb));
    }
    if (t1 <= t0)
        discard;

    int steps = (int)clamp(Params0.w, 8.0, 64.0);
    float stepLen = (t1 - t0) / (float)steps;

    float transmittance = 1.0;
    float3 scattered = float3(0.0, 0.0, 0.0);
    float3 sunDir = normalize(SunDir.xyz);
    float cosSun = dot(dir, sunDir);
    float phase = HenyeyGreenstein(cosSun, Params4.x) * 4.0;
    float phase2 = HenyeyGreenstein(cosSun, -0.25) * 0.35;
    float flash = Params1.z;
    float3 ambientSky = AmbientColor.rgb;
    float3 sunTint = SunColor.rgb * SunDir.w;

    [loop]
    for (int i = 0; i < steps; ++i)
    {
        float t = t0 + stepLen * ((float)i + 0.5);
        float3 p = origin + dir * t;
        float density = SampleDensity(p);
        if (density > 1e-5)
        {
            float lightDepth = 0.0;
            [unroll]
            for (int j = 1; j <= 4; ++j)
            {
                lightDepth += SampleDensity(p + sunDir * (90.0 * (float)j));
            }
            lightDepth *= 90.0;

            float beer = exp(-lightDepth);
            float beer2 = exp(-lightDepth * 0.25);
            float powder = (1.0 - exp(-density * 900.0)) * (1.0 - exp(-density * 900.0));
            float powderTerm = lerp(1.0, powder, Params4.y);
            float3 lightCol = ambientSky * Params4.z
                + sunTint * (beer * (phase * 0.7 + phase2) + beer2 * 0.25) * powderTerm;

            float extinction = exp(-density * stepLen);
            float alpha = 1.0 - extinction;
            scattered += transmittance * alpha * lightCol;
            transmittance *= extinction;
            if (transmittance < 0.015)
                break;
        }
    }

    float alpha = saturate((1.0 - transmittance) * Params1.w);
    if (alpha <= 0.002)
        discard;

    float horizon = exp(-t0 / max(Params4.w, 1.0));
    float storm = saturate(Params5.x);
    float3 color = scattered;
    color = lerp(color, ambientSky, saturate((1.0 - horizon) * 0.35));
    color *= lerp(1.0, 0.35, storm);
    color += sunTint * flash * 0.18;
    return float4(color, alpha);
}
)";
}
