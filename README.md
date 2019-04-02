# VaporPlus
This is a DirectX Raytracing app for testing and reference purposes. It's loosely based off of the D3D12SimpleLighting sample, and has been tested using the raytracing fallback layer.

Includes:
* Mesh and simple mesh loader
* Optional post-process effect
* Direct2D+DirectWrite+Direct3D interop, drawing text onto a textured object

## Build
The project is built against a snapshot of the fallback layer SDK, packaged with this project.
This is organized as a C++ solution built using Microsoft Visual Studio 2017 version 15.7.5.

## Key reference
* A - Rotate the geometry
* P - Toggle a post-process effect
* M - Play music
* A - Spin the geometry
* T - Draw outlines around the text (this is a debugging feature).

![Example image](https://raw.githubusercontent.com/clandrew/vapor/master/Images/Default.gif "Example image")
