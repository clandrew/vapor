#ifndef RAYTRACINGHLSLCOMPAT_H
#define RAYTRACINGHLSLCOMPAT_H

// Workaround for NV driver not supporting null local root signatures. 
// Use an empty local root signature where a shader does not require it.
#define USE_NON_NULL_LOCAL_ROOT_SIG 1

#ifdef HLSL
#include "HlslCompat.h"
#else
using namespace DirectX;

// Shader will use byte encoding to access indices.
typedef UINT16 Index;
#endif

struct SceneConstantBuffer
{
    XMMATRIX projectionToWorld;
    XMVECTOR cameraPosition;
    XMVECTOR lightPosition;
    XMVECTOR lightAmbientColor;
    XMVECTOR lightDiffuseColor;

	XMMATRIX perGeometryTransform[4];

	XMFLOAT3 floorUVDisp;
};

struct CubeConstantBuffer
{
    XMFLOAT4 albedo;
	uint32_t material;
	uint32_t indexBufferOffset;
	uint32_t geometryID;
};

struct Vertex
{
    XMFLOAT3 position;
    XMFLOAT3 normal;
	XMFLOAT3 uv;
};

#define CHECKERBOARD_FLOOR_MATERIAL 1
#define STATUE_MATERIAL 2
#define CITYSCAPE_MATERIAL 3
#define TEXT_MATERIAL 4

enum TextureIdentifier
{
	TextureID_None = -1,

	// During raytracing
	TextureID_Checkerboard = 0,
	TextureID_Cityscape = 2,
	TextureID_Text = 3,

	// During postprocess
	TextureID_TVNoise = 4,

	TextureCount
};

#endif // RAYTRACINGHLSLCOMPAT_H