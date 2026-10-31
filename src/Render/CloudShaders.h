#pragma once


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
    float4 Params6;
    float4 Params7;
    float4 Params8;
    float4 Params9;
    float4 Params10;
    float4 Params11;
    float4 Params12;
    float4 Params13;
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
    float4 Params3;
    float4 Params4;
    float4 Params5;
    float4 Params6;
    float4 Params7;
    float4 Params8;
    float4 Params9;
    float4 Params10;
    float4 Params11;
    float4 Params12;
    float4 Params13;
};

Texture3D<float4> CloudNoise : register(t0);
SamplerState LinearWrapSampler : register(s0);

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
    float3 q = p * (0.0035 * Params1.y * scale) + wind;

    float warp = Fbm(q * 0.55 + 3.1, 2);
    q += float3(warp, warp * 0.6, warp * 0.3) * 1.8;

    float billow = Billow(q);
    float detail = Fbm(q * 3.4 + 21.0, 2);
    if (Params6.x > 0.5)
    {
        float3 uv = q * 0.55;
        float3 n1 = CloudNoise.SampleLevel(LinearWrapSampler, uv, 0).rgb;
        float3 n2 = CloudNoise.SampleLevel(LinearWrapSampler, uv * 2.7 + 0.31, 0).rgb;
        billow = saturate(billow * 0.65 + saturate((n1.r * 0.6 + n1.g * 0.4 - 0.2) * 2.0) * 0.35);
        detail = saturate(detail * 0.5 + n2.b * 0.5);
    }
    float n = billow * 0.78 + detail * 0.22;

    float threshold = lerp(0.78, 0.26, saturate(coverage));
    float d = max(0.0, n - threshold) / max(1.0 - threshold, 1e-4);
    d = saturate(d) * saturate(d + 0.25);

    float verticalProfile = smoothstep(0.0, 0.10, height01) * (1.0 - smoothstep(0.30, 0.85, height01));
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
        return float4(0.85, 0.0, 0.85, 1.0);

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
    float phase = HenyeyGreenstein(cosSun, Params4.x) * 1.2;
    float phase2 = HenyeyGreenstein(cosSun, -0.25) * 0.2;
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
            for (int j = 1; j <= 6; ++j)
            {
                lightDepth += SampleDensity(p + sunDir * (70.0 * (float)j));
            }
            lightDepth *= 70.0;

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
    color = min(color, float3(4.0, 4.0, 4.0));
    return float4(color, alpha);
}
)";

    inline constexpr char kCompositePS[] = R"(
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
    float4 Params6;
    float4 Params7;
    float4 Params8;
    float4 Params9;
    float4 Params10;
    float4 Params11;
    float4 Params12;
    float4 Params13;
};

Texture2D<float4> CloudTex : register(t0);
Texture2D<float4> PrevCloudTex : register(t1);
Texture2D<float> SceneDepth : register(t4);
SamplerState LinearClamp : register(s0);

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    float2 uv = ndc * 0.5 + 0.5;
    if (Params12.y > 0.0)
    {
        const float skyDepth = (Params12.z > 0.5) ? 0.0 : 1.0;
        float sceneDepth = SceneDepth.SampleLevel(LinearClamp, uv, 0);
        if (abs(sceneDepth - skyDepth) > Params12.y)
            discard;
    }
    float4 cur = CloudTex.SampleLevel(LinearClamp, uv, 0);
    float4 col = cur;

    if (Params10.x > 0.001)
    {
        float4 farH = mul(float4(ndc, Params2.z, 1.0), InvViewProj);
        float3 dir = normalize(farH.xyz / farH.w - CameraPos.xyz);
        float4 wp = float4(CameraPos.xyz + dir * 6000.0, 1.0);
        float prevW = dot(Params13, wp);
        float2 prevNdc = float2(0.5, 0.5);
        bool validPrev = abs(prevW) > 1e-6;
        if (validPrev)
            prevNdc = float2(dot(Params7, wp), dot(Params8, wp)) / prevW * 0.5 + 0.5;
        if (validPrev && prevW > 0.0 && all(prevNdc > 0.001) && all(prevNdc < 0.999))
        {
            float4 prev = PrevCloudTex.SampleLevel(LinearClamp, prevNdc, 0);
            col.rgb = lerp(cur.rgb, prev.rgb, Params10.x);
            col.a = lerp(cur.a, prev.a, Params10.x);
        }
    }

    col.rgb = min(col.rgb, float3(4.0, 4.0, 4.0));
    if (col.a <= 0.002)
        discard;
    return float4(col.rgb * col.a, col.a);
}
)";
    inline constexpr char kShadowMaskPS[] = R"(
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
    float4 Params6;
    float4 Params7;
    float4 Params8;
    float4 Params9;
    float4 Params10;
    float4 Params11;
    float4 Params12;
    float4 Params13;
};
Texture3D<float4> CloudNoise : register(t0);
SamplerState LinearWrapSampler : register(s0);

float H13(float3 p)
{
    p = frac(p * 0.1031);
    p += dot(p, p.yzx + 33.33);
    return frac((p.x + p.y) * p.z);
}

float N3(float3 p)
{
    float3 i = floor(p);
    float3 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    float a = lerp(H13(i), H13(i + float3(1, 0, 0)), f.x);
    float b = lerp(H13(i + float3(0, 1, 0)), H13(i + float3(1, 1, 0)), f.x);
    float c = lerp(H13(i + float3(0, 0, 1)), H13(i + float3(1, 0, 1)), f.x);
    float d = lerp(H13(i + float3(0, 1, 1)), H13(i + float3(1, 1, 1)), f.x);
    return lerp(lerp(a, b, f.y), lerp(c, d, f.y), f.z);
}

float FB(float3 p, int oct)
{
    float sum = 0.0;
    float amp = 0.55;
    float3 q = p;
    for (int i = 0; i < oct; ++i)
    {
        sum += N3(q) * amp;
        q = q * 2.02 + 11.7;
        amp *= 0.5;
    }
    return sum;
}

float ShadowCoverage(float3 p, float bottom, float top, float coverage)
{
    float height01 = saturate((p.z - bottom) / max(top - bottom, 1.0));
    if (height01 <= 0.0 || height01 >= 1.0)
        return 0.0;
    float2 windDir = float2(cos(WindAndTime.x), sin(WindAndTime.x)) * WindAndTime.y * 0.0025;
    float3 q = p * (0.00060 * Params1.y) + float3(windDir.x, windDir.y, 0.0) * WindAndTime.z;
    q += float3(FB(q * 0.55 + 3.1, 2), 0.0, 0.0) * 1.2;
    float n = saturate(FB(q, 3) * 1.25);
    float threshold = lerp(0.78, 0.26, saturate(coverage));
    float d = saturate(max(0.0, n - threshold) / max(1.0 - threshold, 1e-4));
    return d * smoothstep(0.0, 0.12, height01) * (1.0 - smoothstep(0.35, 0.9, height01));
}

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    float2 uv = ndc * 0.5 + 0.5;
    float2 worldXY = Params11.xy + (uv - 0.5) * Params11.z;
    float bottom = min(Params0.x, Params3.x);
    float top = max(Params0.y, Params3.y);
    float stepLen = (top - bottom) / 5.0;
    float optical = 0.0;
    [unroll]
    for (int i = 0; i < 5; ++i)
    {
        float3 p = float3(worldXY, bottom + stepLen * ((float)i + 0.5));
        optical += ShadowCoverage(p, Params0.x, Params0.y, Params5.w) * Params0.z * stepLen;
    }
    float shadow = 1.0 - exp(-optical * 0.0012);
    return float4(shadow, shadow, shadow, 1.0);
}
)";

    inline constexpr char kCloudShadowPS[] = R"(
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
    float4 Params6;
    float4 Params7;
    float4 Params8;
    float4 Params9;
    float4 Params10;
    float4 Params11;
    float4 Params12;
    float4 Params13;
};
Texture2D<float> DepthTex : register(t2);
Texture2D<float4> ShadowMask : register(t3);
SamplerState LinearClampSampler : register(s0);

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    float2 uv = ndc * 0.5 + 0.5;
    const bool reversed = Params12.z > 0.5;
    const float skyDepth = reversed ? 0.0 : 1.0;
    float depth = DepthTex.SampleLevel(LinearClampSampler, uv, 0);
    float3 atten = float3(1.0, 1.0, 1.0);
    if (abs(depth - skyDepth) > 0.002)
    {
        float4 worldH = mul(float4(ndc, depth, 1.0), InvViewProj);
        if (abs(worldH.w) > 1e-6)
        {
            float3 world = worldH.xyz / worldH.w;
            float2 suv = (world.xy - Params11.xy) / max(Params11.z, 1.0) + 0.5;
            if (all(suv > 0.0) && all(suv < 1.0))
            {
                float shadow = ShadowMask.SampleLevel(LinearClampSampler, suv, 0).r;
                atten = saturate(1.0 - shadow * Params12.x * saturate(SunDir.z * 2.0));
            }
        }
    }
    return float4(atten, 1.0);
}
)";

    inline constexpr char kShadowDebugPS[] = R"(
Texture2D<float4> ShadowMask : register(t3);
SamplerState LinearClampSampler : register(s0);

float4 main(float4 svPos : SV_Position, float2 ndc : TEXCOORD0) : SV_Target
{
    float2 uv = ndc * 0.5 + 0.5;
    float shadow = ShadowMask.SampleLevel(LinearClampSampler, uv, 0).r;
    return float4(shadow, shadow, shadow, 1.0);
}
)";
}
