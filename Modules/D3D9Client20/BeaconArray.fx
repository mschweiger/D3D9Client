// =================================================================================================================================
// The MIT Lisence:
//
// Copyright (C) 2013 Jarmo Nikkanen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation 
// files (the "Software"), to deal in the Software without restriction, including without limitation the rights to use, copy, 
// modify, merge, publish, distribute, sublicense, and/or sell copies of the Software, and to permit persons to whom the Software 
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// =================================================================================================================================

struct BAVERTEX {                          
    float3 posL     : POSITION0;
    float3 dirL     : NORMAL0;
    float4 data		: TEXCOORD0;
    float2 dataB	: TEXCOORD1;
    float4 colr		: COLOR0;
};


struct BeaconVS
{
    float4 posH     : POSITION0;
    float4 colr     : COLOR0;
    float2 tex0     : TEXCOORD0;
    float  size     : PSIZE;
    float  haze	    : COLOR1;
};


BeaconVS BeaconArrayVS(BAVERTEX vrt)
{
	BeaconVS outVS = (BeaconVS)0;
	float3 posX   = mul(float4(vrt.posL, 1.0f), gW).xyz * gDistScale - gCamOff;
	float dist    = length(posX);	// distance to a beacon
	
	float3 offL   = float3(0, dist*2e-3, 0);
	
	float3 posW   = mul(float4(vrt.posL+offL, 1.0f), gW).xyz;
	float3 dirW   = mul(float4(vrt.dirL, 0.0f), gW).xyz;
	outVS.posH    = mul(float4(posW, 1.0f), gVP);
	
	posW = posW * gDistScale - gCamOff;
	
	float fog     = 1.0f / exp(abs(outVS.posH.z * gFogDensity * 20.0f)); // Haze effect scale factor

	// Viewing angle dependency
	float dota    = -dot(normalize(posW), dirW);
	float angl    = saturate((dota - vrt.data[1])/(1.0f-vrt.data[1])); // beacon visibility from a current point 1.0 to 0.0 
	float disp    = pow(angl, vrt.dataB[1]); // apply a proper curve into a visibility
	
	// Distance attennuation
	float att0    = vrt.dataB[0] / (1.0f+dist*2e-4);
	float att1    = saturate(pow(att0, 2.0f));
	
	if (gTime<vrt.data[2] || gTime>vrt.data[3]) disp = 0.0f;
	
	outVS.colr    = float4(vrt.colr.rgb, min(1, vrt.colr.a * disp * att1));
	outVS.size    = (2.0 + vrt.data[0] * gPointScale / dist) * disp;
	outVS.haze    = clamp(fog*1.8f, 0.3f, 1.8f);
	
    return outVS;
}

float4 BeaconArrayPS(BeaconVS frg) : COLOR
{
    float4 cTex = tex2D(ClampS, frg.tex0);
    //return float4(frg.colr.rgb*saturate(pow(abs(cTex.a),0.3f)*gMix), pow(abs(cTex.a), frg.haze) * frg.colr.a);
    return float4(frg.colr.rgb, pow(abs(cTex.a), frg.haze) * frg.colr.a);
}


technique BeaconArrayTech
{
    pass P0
    {
        vertexShader = compile VS_MOD BeaconArrayVS();
        pixelShader  = compile PS_MOD BeaconArrayPS();

		PointSpriteEnable = true;
        AlphaBlendEnable = true;
		BlendOp = Add; 
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;    
		ZEnable = true;    
        ZWriteEnable = false;
    }
}