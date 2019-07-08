#include "stdafx.h"
#include "VaporPlus.h"

_Use_decl_annotations_
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow)
{
    VaporPlus sample(900, 720, L"VaporPlus");
    return Win32Application::Run(&sample, hInstance, nCmdShow);
}
