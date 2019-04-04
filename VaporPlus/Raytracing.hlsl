//*********************************************************
//
// Copyright (c) Microsoft. All rights reserved.
// This code is licensed under the MIT License (MIT).
// THIS CODE IS PROVIDED *AS IS* WITHOUT WARRANTY OF
// ANY KIND, EITHER EXPRESS OR IMPLIED, INCLUDING ANY
// IMPLIED WARRANTIES OF FITNESS FOR A PARTICULAR
// PURPOSE, MERCHANTABILITY, OR NON-INFRINGEMENT.
//
//*********************************************************

#ifndef RAYTRACING_HLSL
#define RAYTRACING_HLSL

#define HLSL
#include "RaytracingHlslCompat.h"

RaytracingAccelerationStructure Scene : register(t0, space0);
RWTexture2D<float4> RenderTarget : register(u0);
ByteAddressBuffer Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);
Texture2D<float4> CheckerboardTexture : register(t3, space0);
Texture2D<float4> CityscapeTexture : register(t4, space0);
Texture2D<float4> TextTexture : register(t5, space0);

SamplerState TextureSampler : register(s0);

ConstantBuffer<SceneConstantBuffer> g_sceneCB : register(b0);
ConstantBuffer<CubeConstantBuffer> g_perGeometryCB : register(b1);

// Load three 16 bit indices from a byte addressed buffer.
uint3 Load3x16BitIndices(uint offsetBytes)
{
    uint3 indices;

    // ByteAdressBuffer loads must be aligned at a 4 byte boundary.
    // Since we need to read three 16 bit indices: { 0, 1, 2 } 
    // aligned at a 4 byte boundary as: { 0 1 } { 2 0 } { 1 2 } { 0 1 } ...
    // we will load 8 bytes (~ 4 indices { a b | c d }) to handle two possible index triplet layouts,
    // based on first index's offsetBytes being aligned at the 4 byte boundary or not:
    //  Aligned:     { 0 1 | 2 - }
    //  Not aligned: { - 0 | 1 2 }
    const uint dwordAlignedOffset = offsetBytes & ~3;    
    const uint2 four16BitIndices = Indices.Load2(dwordAlignedOffset);
 
    // Aligned: { 0 1 | 2 - } => retrieve first three 16bit indices
    if (dwordAlignedOffset == offsetBytes)
    {
        indices.x = four16BitIndices.x & 0xffff;
        indices.y = (four16BitIndices.x >> 16) & 0xffff;
        indices.z = four16BitIndices.y & 0xffff;
    }
    else // Not aligned: { - 0 | 1 2 } => retrieve last three 16bit indices
    {
        indices.x = (four16BitIndices.x >> 16) & 0xffff;
        indices.y = four16BitIndices.y & 0xffff;
        indices.z = (four16BitIndices.y >> 16) & 0xffff;
    }

    return indices;
}

typedef BuiltInTriangleIntersectionAttributes MyAttributes;
struct RayPayload
{
    float4 color;
	uint recursionDepth;
};
struct ShadowRayPayload
{
	bool hit;
};

// Retrieve hit world position.
float3 HitWorldPosition()
{
    return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], BuiltInTriangleIntersectionAttributes attr)
{
    return vertexAttribute[0] +
        attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
        attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction)
{
    float2 xy = index + 0.5f; // center in the middle of the pixel.
    float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

    // Invert Y for DirectX-style coordinates.
    screenPos.y = -screenPos.y;

    // Unproject the pixel coordinate into a ray.
    float4 world = mul(float4(screenPos, 0, 1), g_sceneCB.projectionToWorld);

    world.xyz /= world.w;
    origin = g_sceneCB.cameraPosition.xyz;
    direction = normalize(world.xyz - origin);
}

// Diffuse lighting calculation.
float4 CalculateDiffuseLighting(float3 incidentLightRay, float3 normal, float4 diffuseColor)
{
	float3 hitToLight = normalize(-incidentLightRay);
	float fNDotL = saturate(dot(hitToLight, normal));

    return g_perGeometryCB.albedo * diffuseColor * fNDotL;
}

[shader("raygeneration")]
void MyRaygenShader()
{
    float3 rayDir;
    float3 origin;
    
    // Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
    GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

    // Trace the ray.
    // Set the ray's extents.
    RayDesc ray;
    ray.Origin = origin;
    ray.Direction = rayDir;
    // Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
    // TMin should be kept small to prevent missing geometry at close contact areas.
    ray.TMin = 0.001;
    ray.TMax = 10000.0;
    RayPayload payload = { float4(0, 0, 0, 0), 0 };
    TraceRay(
		Scene,  // AS
		RAY_FLAG_CULL_BACK_FACING_TRIANGLES, // Flags
		~0, // Mask
		0, // Hit group offset
		2, // Hit group instance multiplier. Note that this sample uses instancing
		0, // Miss shader index
		ray, 
		payload);

    // Write the raytraced color to the output texture.
    RenderTarget[DispatchRaysIndex().xy] = payload.color;
}

[shader("closesthit")]
void MyClosestHitShader(inout RayPayload payload, in MyAttributes attr)
{
    float3 hitPosition = HitWorldPosition();

    // Get the base index of the triangle's first 16 bit index.
    uint indexSizeInBytes = 2;
    uint indicesPerTriangle = 3;
    uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
	uint baseIndex = g_perGeometryCB.indexBufferOffset + (PrimitiveIndex() * triangleIndexStride);
	
    // Load up 3 16 bit indices for the triangle.
    const uint3 indices = Load3x16BitIndices(baseIndex);

    // Retrieve corresponding vertex normals for the triangle vertices.
    float3 vertexNormals[3] = { 
        Vertices[indices[0]].normal, 
        Vertices[indices[1]].normal, 
        Vertices[indices[2]].normal 
    };
	float3 triangleNormal = HitAttribute(vertexNormals, attr);
	triangleNormal = mul(triangleNormal, (float3x3)g_sceneCB.perGeometryTransform[g_perGeometryCB.geometryID]);
	triangleNormal = normalize(mul((float3x3)ObjectToWorld(), triangleNormal));

	float4 reflectionColor = float4(1, 1, 1, 1);
	
	float4 shadow = float4(1, 1, 1, 1);
	float4 reflection = float4(0, 0, 0, 0);


	{
		ShadowRayPayload shadowPayload = { true }; // There's a miss shader to set this to false.

		// Trace shadow ray
		RayDesc rayDesc;
		rayDesc.Origin = hitPosition;
		rayDesc.Direction = normalize(g_sceneCB.lightPosition.xyz - hitPosition);
		rayDesc.TMin = 0.001;
		rayDesc.TMax = 10000.0;

		TraceRay(Scene,
			RAY_FLAG_CULL_BACK_FACING_TRIANGLES
			| RAY_FLAG_ACCEPT_FIRST_HIT_AND_END_SEARCH
			| RAY_FLAG_FORCE_OPAQUE
			| RAY_FLAG_SKIP_CLOSEST_HIT_SHADER,
			~0, // Mask
			1, // RayContributionToHitGroupIndex,
			0, // MultiplierForGeometryContributionToHitGroupIndex, // We're not running closest hit anyhow.
			1, // Miss shader index
			rayDesc,
			shadowPayload);

		if (shadowPayload.hit)
		{
			shadow = float4(0.8f, 0.7f, 0.7f, 1.0f);
		}
	}

	float3 uvs[3] = {
		Vertices[indices[0]].uv,
		Vertices[indices[1]].uv,
		Vertices[indices[2]].uv
	};
	float3 uv = HitAttribute(uvs, attr);

	uint materialIndex = g_perGeometryCB.material;

	float3 incidentLightRay = normalize(hitPosition - g_sceneCB.lightPosition.xyz);

	float4 sampled = float4(1, 1, 1, 1);
	float4 diffuseScale = float4(1, 1, 1, 1);
	float4 specularColor = float4(0, 0, 0, 0);
	float4 lightMaxing = float4(0, 0, 0, 0);

	if (materialIndex == CHECKERBOARD_FLOOR_MATERIAL)
	{
		float2 dispUV = uv.xy;
		dispUV.x += g_sceneCB.floorUVDisp.x;
		dispUV.y += g_sceneCB.floorUVDisp.y;
		sampled = CheckerboardTexture.SampleLevel(TextureSampler, dispUV, 0);
		lightMaxing = float4(1, 1, 1, 1);
	}
	else if(materialIndex == STATUE_MATERIAL)
	{
		sampled = float4(0.8f, 0.8f, 0.75f, 1.0f);

		float3 reflectedLightRay = normalize(reflect(incidentLightRay, triangleNormal));
		float specularPower = 20;
		float4 specularCoefficient = pow(saturate(dot(reflectedLightRay, normalize(-WorldRayDirection()))), specularPower) * 0.5f;
		specularColor = specularCoefficient;
	}
	else if(materialIndex == CITYSCAPE_MATERIAL)
	{
		sampled = CityscapeTexture.SampleLevel(TextureSampler, uv.xy, 0);
	}
	else if (materialIndex == TEXT_MATERIAL)
	{
		sampled = TextTexture.SampleLevel(TextureSampler, uv.xy, 0);
		lightMaxing = float4(1, 1, 1, 1);
	}
	else
	{
		payload.color = float4(1, 1, 1, 1);
	}

	float4 diffuseColor = CalculateDiffuseLighting(incidentLightRay, triangleNormal, g_sceneCB.lightDiffuseColor);

	float4 lightColor = g_sceneCB.lightAmbientColor + diffuseColor + specularColor;
	lightColor = max(lightColor, lightMaxing);

	float4 finalColor = lightColor * sampled * shadow * reflectionColor;
	finalColor += float4(0, 0, 0, 0);

	payload.color = finalColor;
}

[shader("miss")]
void MyMissShader(inout RayPayload payload)
{
    float4 background = float4(1.0f, 0.51, 0.61f, 1.0f);
    payload.color = background;
}

[shader("miss")]
void MyMissShader_ShadowRay(inout ShadowRayPayload shadowPayload)
{
	shadowPayload.hit = false;
}

[shader("miss")]
void MyMissShader_Reflection(inout RayPayload payload)
{
	float4 background = float4(1.0f, 1.0f, 1.0f, 1.0f);
	payload.color = background;
}

#endif // RAYTRACING_HLSL