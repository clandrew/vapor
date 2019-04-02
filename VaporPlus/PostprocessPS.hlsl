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

struct DrawConstants
{
	int enablePostProcess;
	float time;
};
ConstantBuffer<DrawConstants> myDrawConstants : register(b0, space0);

struct PSInput
{
	float4 position : SV_POSITION;
	float2 uv : TEXCOORD;
};

Texture2D g_inputTexture : register(t0);
Texture2D g_tvNoise : register(t1);
SamplerState g_sampler : register(s0);

float4 getLeftMarginColorAdditive(in float2 uv)
{
	float whiteMarginXCutoff = 0.0f;
	whiteMarginXCutoff = sin(uv.y * 5.0f + 4.2f) / 154.0f;
	whiteMarginXCutoff += (1.0f - uv.y) * 0.03f;
	whiteMarginXCutoff += 0.01f;

	float whiteMarginXLimit = whiteMarginXCutoff + 0.015f;

	if (uv.x < whiteMarginXCutoff)
	{
		// On the left hand side, it drops back out to black
		float black = (uv.x + 0.003f) / whiteMarginXCutoff;
		black = sqrt(sqrt(black));
		return float4(black, black, black, 1);

	}
	else if (uv.x < whiteMarginXLimit)
	{
		// Smooth the transition from the non-legible to legible area
		float range = whiteMarginXLimit - whiteMarginXCutoff;
		float fade = (uv.x - whiteMarginXCutoff) / range;
		fade = 1.0f - fade;
		fade = fade * fade;
		return float4(fade, fade, fade, 1);
	}

	return float4(0, 0, 0, 0);
}

float2 getUVAfterBottomMarginEffect(in float2 uv)
{
	// There's a margin at the bottom where the whole image is 
	// condensed down and pushed to the right.    
	float2 modifiedUV = uv;
	float bottomMarginYLimit = 1 - 0.05f;
	if (uv.y > bottomMarginYLimit)
	{
		modifiedUV.x *= modifiedUV.x;
		modifiedUV.y -= bottomMarginYLimit;
		modifiedUV.y *= bottomMarginYLimit / (1 - bottomMarginYLimit);

		// Vary the amount to push to the right, based on Y in the image and on time.
		float pushToRight = 0.5f * modifiedUV.y;

		float oscillate = sin((float)(myDrawConstants.time % 150) * 2 * 3.14159 * 13.0f / 150.0f) * 0.15f;
		float oscillate2 = sin((float)(myDrawConstants.time % 90) * 2 * 3.14159 * 563.0f / 90.0f) * 0.20f;
		pushToRight += oscillate;
		pushToRight += oscillate2;

		modifiedUV.x += pushToRight;
	}
	return modifiedUV;
}

float4 evaluate(float2 uv)
{
	uv = getUVAfterBottomMarginEffect(uv);
	float4 fragColor = g_inputTexture.Sample(g_sampler, float2(uv));

	float4 leftMarginColor = getLeftMarginColorAdditive(uv);
	fragColor += leftMarginColor;
	return fragColor;
}

float4 getNoise(float4 position)
{
	uint noiseXSelect = asuint(myDrawConstants.time) - asuint((float)position.x / 1234.0f);
	uint noiseYSelect = asuint(myDrawConstants.time) - asuint((float)position.y / 4568.0f);
	float noiseX = (noiseXSelect % 1280) / 1280.0f;
	float noiseY = (noiseYSelect % 720) / 720.0f;
	float2 noiseUV = float2(noiseX, (float)noiseY);

	float4 noiseSample = g_tvNoise.Sample(g_sampler, noiseUV);
	noiseSample *= 0.5f;

	return noiseSample;
}

float Grayscale(float4 input)
{
	return (input.r*0.21) + (input.g*0.72) + (input.b*0.07);
}

float GetSnowLines(float2 uv, float greyNoise)
{
	float varying = myDrawConstants.time % 255;
	float lineMovementSpeed = 55;
	float lineDisplace = varying * lineMovementSpeed;
	float lineDensity = 230.0f;
	float lineAlpha = sin(uv.y * lineDensity + lineDisplace);

	float threshold = 0.95f;
	float thresholdVariance = sin(varying / 255.0f) * 0.005f;
	threshold -= thresholdVariance;

	if (lineAlpha > threshold)
	{
		return greyNoise / 4.0f;
	}
	return 0;
}

float4 PSMain(PSInput input) : SV_TARGET
{
	float2 uv = input.uv;

	float4 fragColor;

	if (myDrawConstants.enablePostProcess == 0)
	{
		fragColor = g_inputTexture.Sample(g_sampler, float2(uv));
	}
	else
	{
		// Do multiple samples to create a blur.

		float offset = 0.00065f;
		float bottomMarginYLimit = 1 - 0.02f;
		if (uv.y > bottomMarginYLimit)
		{
			// blur more at the bottom
			offset = 0.015f;
		}

		// Sad horizontal jitter
		float varying = (myDrawConstants.time % (3.14159 * 2)) * 75.0f;
		uv.x += sin(uv.y * 10.0f + varying) / 2000.0f;
		uv.x += sin(uv.y * 99.0f + varying) / 4000.0f;

		float4 s0 = evaluate(float2(uv.x - offset, uv.y - offset));
		float4 s1 = evaluate(float2(uv.x, uv.y - offset));
		float4 s2 = evaluate(float2(uv.x + offset, uv.y - offset));

		float4 s3 = evaluate(float2(uv.x - offset, uv.y));
		float4 s4 = evaluate(float2(uv.x, uv.y));
		float4 s5 = evaluate(float2(uv.x + offset, uv.y));

		float4 s6 = evaluate(float2(uv.x - offset, uv.y + offset));
		float4 s7 = evaluate(float2(uv.x, uv.y + offset));
		float4 s8 = evaluate(float2(uv.x + offset, uv.y + offset));

		// Average the samples
		fragColor = (s0 + s1 + s2 + s3 + s4 + s5 + s6 + s7 + s8) / 9.0f;

		// Apply greyscale
		float grey = Grayscale(fragColor);
		fragColor = grey;
				
		// TV snow
		float4 noiseSample = getNoise(input.position);
		float greyNoise = (1 - Grayscale(noiseSample));
		fragColor *= greyNoise;

		// Snow lines
		float snowLines = GetSnowLines(uv, greyNoise);
		snowLines *= 0.5f;
		fragColor += snowLines;
	}

	return fragColor;
}
