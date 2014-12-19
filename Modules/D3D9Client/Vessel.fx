// ==============================================================
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Dual licensed under GPL v3 and LGPL v3
// Copyright (C) 2014 Jarmo Nikkanen
// ==============================================================

struct VesselVS
{
    float4 posH     : POSITION0;
    float3 CamW     : TEXCOORD0;     
    float3 nrmW     : TEXCOORD1;
    float3 tex0     : TEXCOORD2;
    float2 aux		: TEXCOORD3; 
    float3 diffuse  : COLOR0;           // (Local Light) Diffuse color
    float3 spec     : COLOR1;           // (Local Light) Specular color
	int	   inst		: TEXCOORD4;
};

struct VesselNMVS
{
    float4 posH     : POSITION0;
    float3 camW     : TEXCOORD0;
    float4 locW     : TEXCOORD1;	 // Local light source average dir
    float3 tex0     : TEXCOORD2;
    float3 nrmT     : TEXCOORD3;
    float3 tanT     : TEXCOORD4;
	float3 bitT     : TEXCOORD5;
	float3 diff		: COLOR0;		 // Diffuse color
    float3 spec		: COLOR1;        // Specular color
};


//float3 posW  = mul(float4(vrt.posL, 1.0f), gGrpInst[vrt.idx[0]]).xyz;
//float3 nrmW  = mul(float4(vrt.nrmL, 0.0f), gGrpInst[vrt.idx[0]]).xyz;


// ========================================================================================
// Vertex shader for vessel without normal-mapping
//
VesselVS VesselTechVS(MESH_VERTEX vrt)
{
    VesselVS outVS = (VesselVS)0;
	
	float3 posW  = mul(float4(vrt.posL, 1.0f), gW).xyz;
	float3 nrmW  = mul(float4(vrt.nrmL, 0.0f), gW).xyz;

	outVS.CamW   = -posW * gDistScale + gCamOff;   // A vector from the vertex to the camera
    outVS.nrmW   = nrmW;
	outVS.posH   = mul(float4(posW, 1.0f), gVP);
	outVS.tex0   = float3(vrt.tex0.xy, outVS.posH.z);
	
	if (gLocalLights) {
		float3 locW;
		LocalVertexLight(outVS.diffuse, outVS.spec, locW, nrmW, posW, gMtrl.specular.a);
	}

    // Earth "glow" ------------------------------------------------------------
	if (gGlow) {
		float angl = saturate((-dot(gCameraPos, nrmW)-gProxySize)*gInvProxySize);
		outVS.diffuse.rgb += gAtmColor.rgb * pow(max(0,angl*gGlowConst), 0.7);
	}
	
	// Pre-compute fresnel term ------------------------------------------------
#if defined(_ENVMAP)
	if (gEnvMapEnable) {
		outVS.aux[0] = gMtrl.fresnel.y * pow(1.0f-saturate(dot(normalize(outVS.CamW), nrmW)), gMtrl.fresnel.z);
	}
#endif
	
    return outVS;
}


float4 VesselTechPS(VesselVS frg) : COLOR
{
  
    float3 CamW  = normalize(frg.CamW);
    float3 nrmW  = normalize(frg.nrmW);
    float4 cSpec = gMtrl.specular;
    float4 cTex  = 1;
    float4 cRefl;
    
	if (gTextured) cTex = tex2D(WrapS, frg.tex0.xy);
	
	if (gFullyLit) {
		if (gDebugHL) cTex.rgb = cTex.rgb*0.5 + gColor.rgb;
		return float4(cTex.rgb*saturate(gMtrl.diffuse.rgb+gMtrl.emissive.rgb), cTex.a);
	}

    if (gUseSpec) {
		cSpec = tex2D(SpecS, frg.tex0.xy);
		cSpec.a *= 255.0f;
	}	
    
    cTex.a *= gMtrlAlpha;
    
	// Sunlight calculations. Saturate with cSpec.a to gain an ability to disable specular light
    float  d = saturate(-dot(gSun.direction, nrmW));
    float  s = pow(saturate(dot(reflect(gSun.direction, nrmW), CamW)), cSpec.a) * saturate(cSpec.a);					
    
    if (d==0) s = 0;	
    																					
    float3 diff  = gMtrl.diffuse.rgb * (frg.diffuse.rgb + d * gSun.diffuse.rgb); // Compute total diffuse light
	       diff += (gMtrl.ambient.rgb*gSun.ambient.rgb) + (gMtrl.emissive.rgb);

	float3 cTot  = cSpec.rgb * (frg.spec.rgb + s * gSun.specular.rgb);	// Compute total specular light
	
    cTex.rgb *= saturate(diff);											// Lit the diffuse texture

#if defined(_ENVMAP)

	if (gEnvMapEnable) {
    
		if (gUseRefl) cRefl = tex2D(ReflS, frg.tex0.xy);					// Get a reflection color for non fresnel refl. (Pre-computed intensity in alpha)
		else 		  cRefl = gMtrl.reflect;
		
		cRefl = saturate(cRefl + (cRefl.a>0)*frg.aux[0]);
		
        float3 v = reflect(-CamW, nrmW);								// Reflection vector
		
		// Apply noise/blur effects in reflections
        if (gUseDisl) v += (tex2D(DislMapS, frg.tex0.xy*gMtrl.dislscale)-0.5f) * gMtrl.dislmag;
		
		cTex.rgb *= (1.0f - cRefl.a); 									// Attennuate diffuse texture
		cTot.rgb += cRefl.rgb * texCUBE(EnvMapS, v).rgb;				// Add reflections into a specular light
    }
    
#endif 

#if defined(_ENVMAP) || defined(_GLASS)
	cTex.a = saturate(cTex.a + max(max(cTot.r, cTot.g), cTot.b));		// Re-compute output alpha for alpha blending stage
#endif

	cTex.rgb += cTot.rgb;												// Apply reflections to output color
				
	if (gUseEmis) cTex.rgb += tex2D(EmisS, frg.tex0.xy).rgb;			// Add emissive textures

#if defined(_DEBUG)
    if (gDebugHL) cTex.rgb = cTex.rgb*0.5f + gColor.rgb;				// Apply mesh debugger highlighting
#endif

	if (gInSpace) return cTex;	
	
	float fogFact = exp(-max(0, frg.tex0.z)*gFogDensity);
	return lerp(gFogColor, cTex, fogFact);							// Apply fog
} 




// ========================================================================================
// Vertex shader for vessel with normal-mapping
//
VesselNMVS VesselTechNMVS(MESH_VERTEX vrt)
{
    // Zero output.
	VesselNMVS outVS = (VesselNMVS)0;

    float3 posW = mul(float4(vrt.posL, 1.0f), gW).xyz;
    float3 nrmW = mul(float4(vrt.nrmL, 0.0f), gW).xyz; 
    outVS.posH  = mul(float4(posW, 1.0f), gVP);

    // Construct Tangent space transformation matrix
    float3x3 TBN;
    TBN[0] = vrt.tanL;
    TBN[1] = cross(vrt.tanL, vrt.nrmL);
    TBN[2] = vrt.nrmL; 
    
    TBN = mul(TBN, gW);
    
	outVS.nrmT  = TBN[2];
	outVS.bitT  = TBN[1];
	outVS.tanT  = TBN[0];
    outVS.camW  = -posW * gDistScale + gCamOff;
    outVS.tex0  = float3(vrt.tex0.xy, outVS.posH.z);
   
	float3 locW = 0;

	if (gLocalLights) {
		LocalVertexLight(outVS.diff, outVS.spec, locW, nrmW, posW, gMtrl.specular.a);
	}

    outVS.locW = float4(-locW.xyz, 1.0f - saturate(dot(-locW.xyz, nrmW)));
  
    // Earth "glow" ------------------------------------------------------------
    if (gGlow) {
		float angl = saturate((-dot(gCameraPos, nrmW)-gProxySize)*gInvProxySize);
		outVS.diff.rgb += gAtmColor.rgb * pow(max(0,angl*gGlowConst), 0.7);
	}

    return outVS;
}


float4 VesselTechNMPS(VesselNMVS frg) : COLOR
{
	// Normalize input
	float3 CamW = normalize(frg.camW);
	
	float4 cRefl;
	
	float4 cSpec = gMtrl.specular; 	
    float4 cTex  = tex2D(WrapS, frg.tex0.xy);	
    float3 nrmT  = tex2D(Nrm0S, frg.tex0.xy).rgb * 2.0 - 1.0;       //Sampler for R8G8B8, DXT1
	
	cTex.a*=gMtrlAlpha;
	
	float3x3 TBN;
    TBN[0] = frg.tanT;
    TBN[1] = frg.bitT;
    TBN[2] = frg.nrmT; 

    float3 nrmW = mul(nrmT, TBN);

	if (gUseSpec) {
		cSpec = tex2D(SpecS, frg.tex0);
		cSpec.a *= 255.0f;
	}	
	
	 // Sunlight calculation
    float d = saturate(-dot(gSun.direction, nrmW));
    float s = pow(saturate(dot(reflect(gSun.direction, nrmW), CamW)), cSpec.a) * saturate(cSpec.a);
   			
    if (d==0) s = 0;	
    				
	float local = max(dot(frg.locW.xyz, nrmW) + frg.locW.w, 0.0f);   

    float3 diff = gMtrl.diffuse.rgb * (frg.diff.rgb * (local*local) + d * gSun.diffuse.rgb);
		   diff += (gMtrl.ambient.rgb*gSun.ambient.rgb) + (gMtrl.emissive.rgb);
    
    cTex.rgb *= saturate(diff);
    
    float3 cTot = cSpec.rgb * (frg.spec.rgb + s * gSun.specular.rgb);
    
#if defined(_ENVMAP)
    
    if (gEnvMapEnable) {
		
		if (gUseRefl) cRefl = tex2D(ReflS, frg.tex0.xy);														
		else 		  cRefl = gMtrl.reflect;
		
		float fresnel = gMtrl.fresnel.y * pow(1.0f-saturate(dot(CamW, nrmW)), gMtrl.fresnel.z);
		
		cRefl = saturate(cRefl + (cRefl.a>0)*fresnel); 
	
		cTex.rgb *= (1.0f - cRefl.a); 						
		cTot.rgb += cRefl.rgb * texCUBE(EnvMapS, reflect(-CamW, nrmW)).rgb;					
    }
#endif

	cTex.rgb += cTot.rgb;								// Apply reflections

	if (gUseEmis) cTex.rgb += tex2D(EmisS, frg.tex0.xy).rgb;

#if defined(_DEBUG)	
    if (gDebugHL) cTex.rgb = cTex.rgb*0.5 + gColor.rgb;
#endif

	cTex.a = 1.0f;

    if (gInSpace) return cTex;	
	
	float fogFact = exp(-max(0,frg.tex0.z)*gFogDensity);
	return lerp(gFogColor, cTex, fogFact);							// Apply fog
}




// This is the default mesh rendering technique --------------------------------
//
technique VesselTech
{
    pass P0
    {
        vertexShader = compile VS_MOD VesselTechNMVS();
        pixelShader  = compile PS_MOD VesselTechNMPS();

        AlphaBlendEnable = true;
        BlendOp = Add;
        ZEnable = true; 
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;    
        ZWriteEnable = true;
    }

    pass P1
    {
        vertexShader = compile VS_MOD VesselTechVS();
        pixelShader  = compile PS_MOD VesselTechPS();

        AlphaBlendEnable = true;
        BlendOp = Add;
        ZEnable = true; 
        SrcBlend = SrcAlpha;
        DestBlend = InvSrcAlpha;    
        ZWriteEnable = true;
    }
}