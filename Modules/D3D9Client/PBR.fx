// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2014 - 2016 Jarmo Nikkanen
// ==============================================================


#if defined(_LIGHTS)
struct PBRData
{
	float4 posH     : POSITION0;
	float3 camW     : TEXCOORD0;
	float2 tex0     : TEXCOORD1;
	float3 nrmW     : TEXCOORD2;
	float4 tanW     : TEXCOORD3;	 // Handiness in .w
	// Local vertex lights
	float4 locW     : TEXCOORD4;	 // Local light source average dir
	float3 cDif		: COLOR0;		 // Local lights diffuse color
	float3 cSpe		: COLOR1;		 // Local lights specular color
};
#else
struct PBRData
{
	float4 posH     : POSITION0;
	float3 camW     : TEXCOORD0;
	float2 tex0     : TEXCOORD1;
	float3 nrmW     : TEXCOORD2;
	float4 tanW     : TEXCOORD3;	 // Handiness in .w
	float3 cDif		: COLOR0;		 // Local lights diffuse color
};
#endif


// ========================================================================================================================
// Vertex shader for physics based rendering
//
PBRData PBR_VS(MESH_VERTEX vrt)
{
    // Zero output.
	PBRData outVS = (PBRData)0;

	float3 posW = mul(float4(vrt.posL, 1.0f), gW).xyz;
	float3 nrmW = mul(float4(vrt.nrmL, 0.0f), gW).xyz;
	
	outVS.nrmW = nrmW;
	outVS.tanW = float4(mul(float4(vrt.tanL, 0.0f), gW).xyz, vrt.tex0.z);
	outVS.posH = mul(float4(posW, 1.0f), gVP);
    outVS.camW = -posW;
    outVS.tex0 = vrt.tex0.xy;
	
	// Local light sources ------------------------------------------------------
	//
#if defined(_LIGHTS)
	if (gLocalLights) {	
		float3 locW;
		LocalVertexLight(outVS.cDif, outVS.cSpe, locW, nrmW, posW, gMtrl.specular.a);
		outVS.locW = float4(-locW.xyz, 1.0f - saturate(dot(-locW.xyz, nrmW)));
	}
	else {
		outVS.cDif = 0;
		outVS.cSpe = 0;
		outVS.locW = float4(0, 0, 0, 1);
	}
#else
	outVS.cDif = 0;
#endif


	// Earth "glow" ------------------------------------------------------------
	//
	if (gGlow) {
		float angl = saturate((-dot(gCameraPos, nrmW) - gProxySize) * gInvProxySize);
		outVS.cDif += gAtmColor.rgb * max(0, angl*gGlowConst);
	}

    return outVS;
}






// ========================================================================================================================
//
float4 PBR_PS(PBRData frg) : COLOR
{
	float3 nrmT;
	float3 nrmW;
	float3 cEmis;
	float3 cRefl;
	float3 cFrsl;
	float4 cDiff;
	float4 cSpec;
	float4 sMask = float4(1, 1, 1, 1024);
	float  fRghn;


	// ----------------------------------------------------------------------
	// Start fetching texture data 
	// ----------------------------------------------------------------------

	if (gTextured) cDiff = tex2D(WrapS, frg.tex0.xy);
	else		   cDiff = 1;


	// Fetch a normal map 
	//
	if (gCfg.Norm) nrmT = tex2D(Nrm0S, frg.tex0.xy).rgb;

	// Use _refl color for both
	if (gCfg.Refl) cRefl = tex2D(ReflS, frg.tex0.xy).rgb;
	else		   cRefl = gMtrl.reflect.rgb;

	// Roughness map 
	if (gCfg.Rghn) fRghn = tex2D(RghnS, frg.tex0.xy).g;
	else		   fRghn = gMtrl.roughness;

	// Sample specular map
	if (gCfg.Spec) cSpec = tex2D(RghnS, frg.tex0.xy).rgba * sMask;
	else           cSpec = gMtrl.specular.rgba;

	// Sample emission map. (Note: Emissive materials and textures need to go different stages, material is added to light)
	if (gCfg.Emis) cEmis = tex2D(EmisS, frg.tex0.xy).rgb;
	else		   cEmis = 0;

#if defined(_GLASS)
	// Sample fresnel map
	if (gCfg.Frsl) cFrsl = tex2D(FrslS, frg.tex0.xy).rgb;
	else 		   cFrsl = gMtrl.fresnel.y;
#endif




	// ----------------------------------------------------------------------
	// Now do other calculations while textures are being fetched 
	// ----------------------------------------------------------------------

	float3 CamD = normalize(frg.camW);
	float3 cMrtlBase = (gMtrl.ambient.rgb*gSun.Ambient) + gMtrl.emissive.rgb;
	float3 cSun = saturate(gSun.Color) * fSunIntensity;
	float  fN = 1.0f;




	// ----------------------------------------------------------------------
	// Texture tuning controls for add-on developpers
	// ----------------------------------------------------------------------

#if defined(_DEBUG)
	if (gTuneEnabled) {

		nrmT *= gTune.Norm.rgb;

		cDiff.rgb = pow(abs(cDiff.rgb), gTune.Albe.a) * gTune.Albe.rgb;
		cRefl.rgb = pow(abs(cRefl.rgb), gTune.Refl.a) * gTune.Refl.rgb;
		cEmis.rgb = pow(abs(cEmis.rgb), gTune.Emis.a) * gTune.Emis.rgb;
		cFrsl.rgb = pow(abs(cFrsl.rgb), gTune.Frsl.a) * gTune.Frsl.rgb;
		fRghn = pow(abs(fRghn), gTune.Rghn.a) * gTune.Rghn.g;
		cSpec.rgba = cSpec.rgba * gTune.Spec.rgba;
		
		cDiff = saturate(cDiff);
		cRefl = saturate(cRefl);
		cFrsl = saturate(cFrsl);
		fRghn = saturate(fRghn);
		cSpec = min(cSpec, sMask);
	}
#endif


	// ----------------------------------------------------------------------
	// Convert reflection map from sRGB to Linear
	// ----------------------------------------------------------------------

	float3 cRefl2 = cRefl*cRefl;
	float3 cRefl3 = cRefl2*cRefl;
	float fRefl = cmax(cRefl3);



	// ----------------------------------------------------------------------
	// "Legacy/PBR" switch
	// ----------------------------------------------------------------------

	if (gPBRSw) {
		cSpec.rgb = cRefl2;
		cSpec.a = exp2(fRghn * 12.0f);					// Compute specular power
	}


	

	// ----------------------------------------------------------------------
	// Construct a proper world space normal
	// ----------------------------------------------------------------------

	if (gCfg.Norm) {
		float3 bitW = cross(frg.tanW.xyz, frg.nrmW) * frg.tanW.w;
		nrmT.rg = nrmT.rg * 2.0f - 1.0f;
		nrmW = frg.nrmW*nrmT.z + frg.tanW.xyz*nrmT.x + bitW*nrmT.y;
	#if defined(_LIGHTS)
		fN = max(dot(frg.locW.xyz, nrmW) + frg.locW.w, 0.0f);
		fN *= fN;
	#endif
	}
	else nrmW = frg.nrmW;

	nrmW = normalize(nrmW);



	// ----------------------------------------------------------------------
	// Compute reflection vector and some required dot products 
	// ----------------------------------------------------------------------

	float3 RflW = reflect(-CamD, nrmW);
	float dRS = saturate(-dot(RflW, gSun.Dir));
	float dLN = saturate(-dot(gSun.Dir, nrmW));
	float dLNx = saturate(dLN * 20.0f);


	// ----------------------------------------------------------------------
	// Compute a fresnel terms fFrsl, iFrsl, fFLbe 
	// ----------------------------------------------------------------------

	float fFrsl = 0;	// Fresnel angle co-efficiency factor
	float iFrsl = 0;	// Fresnel intensity
	float fFLbe = 0;	// Fresnel lobe
	
#if defined(_GLASS)

	if (gFresnel) {

		float dCN = saturate(dot(CamD, nrmW));

		// Compute a fresnel term 
		fFrsl = pow(1.0f - dCN, gMtrl.fresnel.x);

		// Compute a specular lobe for fresnel reflection
		fFLbe = pow(dRS, gMtrl.fresnel.z) * dLNx * any(cRefl);

		// Compute intensity term. Fresnel is always on a top of multi-layer material
		// therefore it remains strong and attennuates other properties to maintain energy conservation using (1.0 - iFrsl)
		iFrsl = cmax(cFrsl) * fFrsl;
	}
#endif




	// ----------------------------------------------------------------------
	// Compute a specular and diffuse lighting
	// ----------------------------------------------------------------------

	// Compute a specular lobe for base material
	float fLobe = pow(dRS, cSpec.a) * dLNx;

	// Compute received diffuse light
	float3 diffLight = dLN * cSun * fDiffuseFactor + frg.cDif * fN;

	// Bake material props and lights together
	float3 diffBaked = (gMtrl.diffuse.rgb*diffLight) + cMrtlBase;

#if defined(_LIGHTS)
	cSun += frg.cSpe;	// Add local light sources
#endif

	// Special alpha only texture in use
	if (gNoColor) cDiff.rgb = 1;

	// ------------------------------------------------------------------------
	cDiff.rgb *= diffBaked;				// Lit the texture
	cDiff.a *= gMtrlAlpha;				// Modulate material alpha


	// ------------------------------------------------------------------------
	// Compute total reflected sun light from a material
	//
	float3 cBase = cSpec.rgb * (1.0f - iFrsl) * fLobe;

#if defined(_GLASS)
	cBase += cFrsl.rgb * fFrsl * fFLbe;
#endif

	cSpec.rgb = cSun * saturate(cBase);
	cSpec.rgb *= 1.5f;  // Give a boost for a better look
	






	// ----------------------------------------------------------------------
	// Compute a environment reflections
	// ----------------------------------------------------------------------

	float3 cEnv = 0;
	
#if defined(_ENVMAP)

	if (gEnvMapEnable) {

#if defined(_GLASS)

		if (gFresnel) {
			
			// Compute LOD level for fresnel reflection
			float fLOD = (8.0f - log2(gMtrl.fresnel.z) * 0.666666f);
			
			// Always mirror clear reflection for low angles
			fLOD *= (1.0f - fFrsl);

			// Fresnel based environment reflections
			cEnv = (cFrsl * fFrsl) * texCUBElod(EnvMapAS, float4(RflW, fLOD)).rgb;
		}
#endif

		// Compute LOD level for blur effect 
		float fLOD = (1.0f - fRghn) * 8.0f;

		// Add a metallic reflections from a base material 
		cEnv += cRefl3 * (1.0f-iFrsl) * texCUBElod(EnvMapAS, float4(RflW, fLOD)).rgb;
	}

#endif	




	// ----------------------------------------------------------------------
	// Combine all results together
	// ----------------------------------------------------------------------

	// Compute total reflected light
	float fTot = cmax(cEnv + cSpec.rgb);

#if defined(_ENVMAP)
	// Attennuate diffuse surface beneath
	cDiff.rgb *= (1.0f - fRefl);

#if defined(_GLASS)
	cDiff.rgb *= (1.0f - iFrsl*iFrsl);			// note: (1-iFrsl) goes black too quick
#endif	
#endif

	// Re-compute output alpha for alpha blending stage
	cDiff.a = saturate(cDiff.a + fTot);

	// Add reflections to output
	cDiff.rgb += cEnv;

	// Add specular to output
	cDiff.rgb += cSpec.rgb;

	// Add emission texture to output, modulate with material
	cDiff.rgb = max(cDiff.rgb, cEmis * gMtrl.emission2.rgb);

#if defined(_DEBUG)	
	if (gDebugHL) cDiff = cDiff*0.5f + gColor;
#endif

	return cDiff;
}







// ================================================================================================
// Fast legacy Implementation no additional textures
// ================================================================================================


#if defined(_LIGHTS)
struct FASTData
{
	float4 posH     : POSITION0;
	float3 camW     : TEXCOORD0;
	float2 tex0     : TEXCOORD1;
	float3 nrmW     : TEXCOORD2;
	float3 cDif		: COLOR0;		 // Local lights diffuse color
	float3 cSpe		: COLOR1;		 // Local lights specular color
};
#else
struct FASTData
{
	float4 posH     : POSITION0;
	float3 camW     : TEXCOORD0;
	float2 tex0     : TEXCOORD1;
	float3 nrmW     : TEXCOORD2;
	float3 cDif		: COLOR0;		 // Local lights diffuse color
};
#endif


// ========================================================================================================================
// Vertex shader for physics based rendering
//
FASTData FAST_VS(MESH_VERTEX vrt)
{
	// Zero output.
	FASTData outVS = (FASTData)0;

	float3 posW = mul(float4(vrt.posL, 1.0f), gW).xyz;
	float3 nrmW = mul(float4(vrt.nrmL, 0.0f), gW).xyz;

	outVS.nrmW = nrmW;
	outVS.posH = mul(float4(posW, 1.0f), gVP);
	outVS.camW = -posW;
	outVS.tex0 = vrt.tex0.xy;

	// Local light sources ------------------------------------------------------
	//
#if defined(_LIGHTS)
	if (gLocalLights) {
		float3 locW;
		LocalVertexLight(outVS.cDif, outVS.cSpe, locW, nrmW, posW, gMtrl.specular.a);
	}
	else {
		outVS.cDif = 0;
		outVS.cSpe = 0;	
	}
#else
	outVS.cDif = 0;
#endif


	// Earth "glow" ------------------------------------------------------------
	//
	if (gGlow) {
		float angl = saturate((-dot(gCameraPos, nrmW) - gProxySize) * gInvProxySize);
		outVS.cDif += gAtmColor.rgb * max(0, angl*gGlowConst);
	}

	return outVS;
}


// ========================================================================================================================
//
float4 FAST_PS(FASTData frg) : COLOR
{

	float3 cEmis;
	float4 cDiff;
	
	// Start fetching texture data -------------------------------------------
	//
	if (gTextured) cDiff = tex2D(WrapS, frg.tex0.xy);
	else		   cDiff = 1;

	if (gFullyLit) {
		if (gNoColor) cDiff.rgb = 1;
		cDiff.rgb *= saturate(gMtrl.diffuse.rgb + gMtrl.emissive.rgb);
	}
	else {

		// Sample emission map. (Note: Emissive materials and textures need to go different stages, material is added to light)
		if (gCfg.Emis) cEmis = tex2D(EmisS, frg.tex0.xy).rgb;
		else		   cEmis = 0;

		float3 nrmW  = normalize(frg.nrmW);
		float4 cSpec = gMtrl.specular.rgba;
		float3 cSun  = saturate(gSun.Color) * fSunIntensity;
		float  dLN   = saturate(-dot(gSun.Dir, nrmW));
		
		cSpec.rgb *= 0.33333f;

		if (gNoColor) cDiff.rgb = 1;

		cDiff.rgb *= (gMtrl.diffuse.rgb*(dLN * cSun * fDiffuseFactor + frg.cDif)) + (gMtrl.ambient.rgb*gSun.Ambient) + gMtrl.emissive.rgb;

		float3 CamD = normalize(frg.camW);
		float3 HlfW = normalize(CamD - gSun.Dir);
		float  fSun = pow(saturate(dot(HlfW, nrmW)), gMtrl.specular.a);

		if (dLN == 0) fSun = 0;

#if defined(_LIGHTS)
		float3 specLight = (fSun * cSun) + frg.cSpe;
#else
		float3 specLight = (fSun * cSun);
#endif
		cDiff.rgb += (cSpec.rgb * specLight);
		
		cDiff.rgb += cEmis;
	}

#if defined(_DEBUG)	
	if (gDebugHL) cDiff = cDiff*0.5f + gColor;
#endif

	cDiff.a *= gMtrlAlpha;

	return cDiff;
}