#define STANDARD_MAX_INSTANCED_BLOCK 32

struct PerInstanceData
{
    matrix MatWorldNoScale;
    float4 Color;
    float3 From;
    float Padding1;
    float3 To;
    float Padding2;
};

struct VS_INPUT
{
    float3 Position : POSITION;
};

struct VS_OUTPUT
{
    float4 Position : SV_POSITION;
};

struct GS_INPUT
{
    float4 Position : SV_POSITION;
};

struct GS_OUTPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};

struct PS_INPUT
{
    float4 Position : SV_POSITION;
    float4 Color : COLOR0;
};