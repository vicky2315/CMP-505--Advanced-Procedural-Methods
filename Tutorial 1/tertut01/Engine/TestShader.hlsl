Texture2D shaderTexture : register(t0);
SamplerState SampleType : register(s0);


cbuffer LightBuffer : register(b0)
{
    float4 ambientColor;
    float4 diffuseColor;
    float3 lightPosition;
    float padding;
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
    float4 textureColor;
    float3 lightDir;
    float lightIntensity;
    float4 color, baseColor;
    float4 trueColor;
    
    
    baseColor = shaderTexture.Sample(SampleType, input.tex);
    lightDir = normalize(input.position3D - lightPosition);
    lightIntensity = saturate(dot(input.normal, -lightDir));
    //float diffuseLightingFactor = saturate(dot(-normalize(LightDir), PSIn.Normal)) * LightPower;
    //color = ambientColor + (diffuseColor * lightIntensity); //adding ambient
    trueColor = baseColor * (diffuseColor + ambientColor);
    float greyscaleAverage = (trueColor.r + trueColor.g + trueColor.b) / 3.0f;
    color = float4(greyscaleAverage, greyscaleAverage, greyscaleAverage, trueColor.a);
    
    //textureColor = shaderTexture.Sample(SampleType, input.tex);
    return color;
}

