// pch.h: This is a precompiled header file.
// Files listed below are compiled only once, improving build performance for future builds.
// This also affects IntelliSense performance, including code completion and many code browsing features.
// However, files listed here are ALL re-compiled if any one of them is updated between builds.
// Do not add files here that you will be updating frequently as this negates the performance advantage.

#ifndef PCH_H
#define PCH_H

// add headers that you want to pre-compile here
#include <windows.h>

// C RunTime Header Files
#include <stdlib.h>
#include <sstream>
#include <iomanip>

#include <list>
#include <string>
#include <wrl.h>
#include <shellapi.h>
#include <memory>
#include <unordered_map>
#include <vector>
#include <string>
#include <fstream>
#include <atlbase.h>
#include <assert.h>

#include <initguid.h>
#include <dxgi1_6.h>
#include "d3d12.h"
#include <Mmsystem.h>
#include "D3D12RaytracingHelpers.hpp"
#include "d3dx12.h"
#include <wincodec.h>
#include <d3d11on12.h>
#include <d2d1_3.h>
#include <dwrite.h>

#include <DirectXMath.h>

#ifdef _DEBUG
#include <dxgidebug.h>
#endif

#include "DXSampleHelper.h"
#include "DeviceResources.h"

#endif //PCH_H
