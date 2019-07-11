# VaporPlus
This is a DirectX Raytracing app for testing and reference purposes. It's loosely based off of the D3D12SimpleLighting sample, and is built against the DXR API available on RS5, i.e., Windows 10 October 2018 Update.

Includes:
* Mesh and simple mesh loader
* Optional post-process effect
* Direct2D+DirectWrite+Direct3D interop, drawing text onto a textured object

![Example image](https://raw.githubusercontent.com/clandrew/vapor/master/Images/Default.gif "Example image")

## Build
The project is built against a snapshot of the fallback layer SDK, packaged with this project.
This is organized as a C++ solution built using Microsoft Visual Studio 2017 version 15.7.5.

## Key reference
* A - Spin the geometry
* P - Toggle a post-process effect
* M - Play music
* T - Draw outlines around the text (this is a debugging feature).

## Tested platforms
The sample has been tested on NVIDIA GeForce RTX 2080, and NVIDIA GeForce GTX 1070 with a DXR-on-GTX compatible driver.

## Earlier FL support
Earlier versions of this proejct worked against a snapshot of the fallback layer SDK, packaged with the project. These earlier versions are commit [5c60013](https://github.com/clandrew/vapor/commit/5c600131c5633cd7eb85a31c3b14c5730a89ad90) and earlier.
