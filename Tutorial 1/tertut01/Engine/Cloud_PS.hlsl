Texture2D VolumeTexture : register(t0);
SamplerState VolumeSampler : register(s0);

cbuffer LightBuffer : register(b0)
{
    float4 ambientColor;
    float4 diffuseColor;
    float3 lightPosition;
    float fogDensity;
};

struct InputType
{
    float4 position : SV_POSITION;
    float2 tex : TEXCOORD0;
    float3 normal : NORMAL;
    float3 position3D : TEXCOORD2;
};

float4 main(InputType input) : SV_TARGET
{
    // Sample the volume texture using the texture coordinates
    float density = VolumeTexture.Sample(VolumeSampler, input.tex);
    
    // Apply fog density to the color
    float fogFactor = exp(-fogDensity * density.r);

    // Blend fog color with the background color (e.g., sky color)
    float4 fogColor = float4(0.0, 0.0, 0.0, 0.0); // Adjust color as needed
    float4 backgroundColor = float4(0.0, 0.0, 0.0, 0.0); // Example background color
    float4 finalColor = lerp(backgroundColor, fogColor, fogFactor);

    return finalColor;
}