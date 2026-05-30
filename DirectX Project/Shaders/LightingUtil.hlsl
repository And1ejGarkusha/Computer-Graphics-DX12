#define MaxLights 16

static const float PI = 3.14159265359f;

struct Light
{
    float3 Strength;
    float FalloffStart;
    float3 Direction;
    float FalloffEnd;
    float3 Position;
    float SpotPower;
};

struct Material
{
    float4 DiffuseAlbedo;
    float3 FresnelR0;
    float Roughness;
    float Metallic;
};

float DistributionGGX(float3 N, float3 H, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0f);
    float NdotH2 = NdotH * NdotH;
    float denom = NdotH2 * (a2 - 1.0f) + 1.0f;
    denom = PI * denom * denom;
    return a2 / denom;
}

//Schlick-GGX geometry term (for direct lights: k = (r+1)**2/8)
float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = roughness + 1.0f;
    float k = (r * r) / 8.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-7f);
}

//Smith's method
float GeometrySmith(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX(NdotV, roughness) * GeometrySchlickGGX(NdotL, roughness);
}

//Schlick-GGX geometry for IBL (k = r**2/2)
float GeometrySchlickGGX_IBL(float NdotV, float roughness)
{
    float a = roughness;
    float k = (a * a) / 2.0f;
    return NdotV / max(NdotV * (1.0f - k) + k, 1e-7f);
}

float GeometrySmith_IBL(float3 N, float3 V, float3 L, float roughness)
{
    float NdotV = max(dot(N, V), 0.0f);
    float NdotL = max(dot(N, L), 0.0f);
    return GeometrySchlickGGX_IBL(NdotV, roughness) * GeometrySchlickGGX_IBL(NdotL, roughness);
}

//Fresnel-Schlick
float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (1.0f - F0) * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

//Fresnel-Schlick with roughness (for IBL ambient)
float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    return F0 + (max(float3(1.0f - roughness, 1.0f - roughness, 1.0f - roughness), F0) - F0)
               * pow(clamp(1.0f - cosTheta, 0.0f, 1.0f), 5.0f);
}

float CalcAttenuation(float d, float falloffStart, float falloffEnd)
{
    return saturate((falloffEnd - d) / (falloffEnd - falloffStart));
}

float3 CookTorranceBRDF(float3 lightStrength, float3 L, float3 N, float3 V, Material mat)
{
    float3 albedo = mat.DiffuseAlbedo.rgb;
    float rough = max(mat.Roughness, 0.04f);
    float metallic = mat.Metallic;
    
    float3 F0 = lerp(float3(0.04f, 0.04f, 0.04f), albedo, metallic);

    float3 H = normalize(V + L);

    float NDF = DistributionGGX(N, H, rough);
    float G = GeometrySmith(N, V, L, rough);
    float3 F = FresnelSchlick(max(dot(H, V), 0.0f), F0);

    float NdotL = max(dot(N, L), 0.0f);
    float NdotV = max(dot(N, V), 0.0f);

    float3 numerator = NDF * G * F;
    float denominator = 4.0f * NdotV * NdotL;
    float3 specular = numerator / max(denominator, 1e-4f);

    float3 kS = F;
    float3 kD = (1.0f - kS) * (1.0f - metallic);
    
    float3 diffuse = kD * albedo / PI;

    return (diffuse + specular) * lightStrength * NdotL;
}

float3 ComputeDirectionalLight(Light L, Material mat, float3 N, float3 V)
{
    float3 lightDir = -L.Direction;
    float NdotL = max(dot(lightDir, N), 0.0f);
    float3 lightStrength = L.Strength;
    return CookTorranceBRDF(lightStrength, lightDir, N, V, mat);
}

float3 ComputePointLight(Light L, Material mat, float3 pos, float3 N, float3 V)
{
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);
    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d;
    float NdotL = max(dot(lightVec, N), 0.0f);
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    float3 lightStrength = L.Strength * att;
    return CookTorranceBRDF(lightStrength, lightVec, N, V, mat);
}

float3 ComputeSpotLight(Light L, Material mat, float3 pos, float3 N, float3 V)
{
    float3 lightVec = L.Position - pos;
    float d = length(lightVec);
    if (d > L.FalloffEnd)
        return 0.0f;

    lightVec /= d;
    float NdotL = max(dot(lightVec, N), 0.0f);
    float att = CalcAttenuation(d, L.FalloffStart, L.FalloffEnd);
    float spotFactor = pow(max(dot(-lightVec, L.Direction), 0.0f), L.SpotPower);
    float3 lightStrength = L.Strength * att * spotFactor;
    return CookTorranceBRDF(lightStrength, lightVec, N, V, mat);
}

float4 ComputeLighting(Light gLights[MaxLights], Material mat,
                       float3 pos, float3 N, float3 V,
                       float3 shadowFactor)
{
    float3 result = 0.0f;
    int i = 0;

#if (NUM_DIR_LIGHTS > 0)
    for (i = 0; i < NUM_DIR_LIGHTS; ++i)
        result += shadowFactor[i] * ComputeDirectionalLight(gLights[i], mat, N, V);
#endif

#if (NUM_POINT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS; i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS; ++i)
        result += ComputePointLight(gLights[i], mat, pos, N, V);
#endif

#if (NUM_SPOT_LIGHTS > 0)
    for (i = NUM_DIR_LIGHTS + NUM_POINT_LIGHTS;
         i < NUM_DIR_LIGHTS + NUM_POINT_LIGHTS + NUM_SPOT_LIGHTS; ++i)
        result += ComputeSpotLight(gLights[i], mat, pos, N, V);
#endif

    return float4(result, 0.0f);
}
