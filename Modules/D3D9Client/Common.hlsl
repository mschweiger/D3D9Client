
#define KERNEL_RADIUS 2.0f
#define SHADOW_THRESHOLD 0.1f       // 0.3 to 7.0




// ==========================================================================================================
// Local light sources
// ==========================================================================================================


float3 Light_fx(float3 x)
{
	return saturate(x);  //1.5 - exp2(-x.rgb)*1.5f;
}

void LocalLights(
	out float3 diff_out,
	out float3 spec_out,
	in float3 nrmW,
	in float3 posW,
	in float sp,
	uniform int x,
	uniform bool bSpec)
{

	float3 posWN = normalize(-posW);
	float3 p[4];
	float4 spe;
	int i;

	// Relative positions
	[unroll] for (i = 0; i < 4; i++) p[i] = posW - gLights[i + x].position;

	// Square distances
	float4 sd;
	[unroll] for (i = 0; i < 4; i++) sd[i] = dot(p[i], p[i]);

	// Normalize
	sd = rsqrt(sd);
	[unroll] for (i = 0; i < 4; i++) p[i] *= sd[i];

	// Distances
	float4 dst = rcp(sd);

	// Attennuation factors
	float4 att;
	[unroll] for (i = 0; i < 4; i++) att[i] = dot(gLights[i + x].attenuation.xyz, float3(1.0, dst[i], dst[i] * dst[i]));

	att = rcp(att);

	// Spotlight factors
	float4 spt;
	[unroll] for (i = 0; i < 4; i++) {
		spt[i] = (dot(p[i], gLights[i + x].direction) - gLights[i + x].param[Phi]) * gLights[i + x].param[Theta];
		if (gLights[i + x].type == 0) spt[i] = 1.0f;
	}

	spt = saturate(spt);

	// Diffuse light factors
	float4 dif;
	[unroll] for (i = 0; i < 4; i++) dif[i] = dot(-p[i], nrmW);

	dif = saturate(dif);
	dif *= (att*spt);

	// Specular lights factors
	if (bSpec) {

		[unroll] for (i = 0; i < 4; i++) spe[i] = dot(reflect(p[i], nrmW), posWN) * (dif[i] > 0);

		spe = pow(saturate(spe), sp);
		spe *= (att*spt);
	}

	diff_out = 0;
	spec_out = 0;

	[unroll] for (i = 0; i < 4; i++) diff_out += gLights[i].diffuse.rgb * dif[i];

	if (bSpec) {
		[unroll] for (i = 0; i < 4; i++) spec_out += gLights[i].diffuse.rgb * spe[i];
	}
}




void LocalLightsEx(out float3 cDiffLocal, out float3 cSpecLocal, in float3 nrmW, in float3 posW, in float sp)
{

#if LMODE !=0
	if (!gLightsEnabled) {
		cDiffLocal = 0;
		cSpecLocal = 0;
	}
#endif

#if LMODE == 1
	LocalLights(cDiffLocal, cSpecLocal, nrmW, posW, sp, 0, false);

#elif LMODE == 2
	LocalLights(cDiffLocal, cSpecLocal, nrmW, posW, sp, 0, true);

#elif LMODE == 3
	float3 dd, ss;
	LocalLights(cDiffLocal, cSpecLocal, nrmW, posW, sp, 0, false);
	LocalLights(dd, ss, nrmW, posW, sp, 4, false);
	cDiffLocal += dd;
	cSpecLocal += ss;

#elif LMODE == 4
	float3 dd, ss;
	LocalLights(cDiffLocal, cSpecLocal, nrmW, posW, sp, 0, true);
	LocalLights(dd, ss, nrmW, posW, sp, 4, true);
	cDiffLocal += dd;
	cSpecLocal += ss;
#else
	cDiffLocal = 0;
	cSpecLocal = 0;
#endif
}







// ==========================================================================================================
// Object Self Shadows
// ==========================================================================================================

float ProjectShadows(float2 sp)
{
	if (!gShadowsEnabled) return 1.0f;

	if (sp.x < 0 || sp.y < 0) return 1.0f;
	if (sp.x > 1 || sp.y > 1) return 1.0f;

	float2 dx = float2(gSHD[1], 0) * 1.5f;
	float2 dy = float2(0, gSHD[1]) * 1.5f;
	float  va = 0;
	float  pd = 1e-4;

	sp -= dy;
	if ((tex2D(ShadowS, sp - dx).r) < pd) va++;
	if ((tex2D(ShadowS, sp).r) < pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) < pd) va++;
	sp += dy;
	if ((tex2D(ShadowS, sp - dx).r) < pd) va++;
	if ((tex2D(ShadowS, sp).r) < pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) < pd) va++;
	sp += dy;
	if ((tex2D(ShadowS, sp - dx).r) < pd) va++;
	if ((tex2D(ShadowS, sp).r) < pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) < pd) va++;

	return va / 9.0f;
}


// ---------------------------------------------------------------------------------------------------
//
float SampleShadows(float2 sp, float pd)
{
	if (!gShadowsEnabled) return 1.0f;

	float2 dx = float2(gSHD[1], 0) * 1.5f;
	float2 dy = float2(0, gSHD[1]) * 1.5f;
	float  va = 0;

	sp -= dy;
	if ((tex2D(ShadowS, sp - dx).r) > pd) va++;
	if ((tex2D(ShadowS, sp).r) > pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) > pd) va++;
	sp += dy;
	if ((tex2D(ShadowS, sp - dx).r) > pd) va++;
	if ((tex2D(ShadowS, sp).r) > pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) > pd) va++;
	sp += dy;
	if ((tex2D(ShadowS, sp - dx).r) > pd) va++;
	if ((tex2D(ShadowS, sp).r) > pd) va++;
	if ((tex2D(ShadowS, sp + dx).r) > pd) va++;

	return va * 0.1111111f;
}


// ---------------------------------------------------------------------------------------------------
//
float SampleShadows2(float2 sp, float pd)
{
	if (!gShadowsEnabled) return 1.0f;

	float val = 0;
	float m = KERNEL_RADIUS * gSHD[1];

	[unroll] for (int i = 0; i < KERNEL_SIZE; i++) {
		if ((tex2D(ShadowS, sp + kernel[i].xy * m).r) > pd) val += kernel[i].z;
	}

	return saturate(val * KERNEL_WEIGHT);
}


// ---------------------------------------------------------------------------------------------------
//
float SampleShadows3(float2 sp, float pd, float4 frame)
{
	if (!gShadowsEnabled) return 1.0f;

	float val = 0;
	frame *= KERNEL_RADIUS * gSHD[1];

	[unroll] for (int i = 0; i < KERNEL_SIZE; i++) {
		float2 ofs = frame.xy*kernel[i].x + frame.zw*kernel[i].y;
		if ((tex2D(ShadowS, sp + ofs).r) > pd) val += kernel[i].z;
	}

	return saturate(val * KERNEL_WEIGHT);
}


// ---------------------------------------------------------------------------------------------------
//
float SampleShadowsEx(float2 sp, float pd, float4 sc)
{
#if SHDMAP == 1
	return SampleShadows(sp, pd);
#elif SHDMAP == 2 || SHDMAP == 4
	return SampleShadows2(sp, pd);
#else
	float si, co;
	sc += (gSHD[2] * 2.0f);
	sincos(sc.y + sc.x*149.0f, si, co);
	return SampleShadows3(sp, pd, float4(si, co, co, -si));
#endif
}


// ---------------------------------------------------------------------------------------------------
//
float ComputeShadow(float4 shdH, float dLN, float4 sc)
{
	shdH.xyz /= shdH.w;
	float2 sp = shdH.xy * float2(0.5f, -0.5f) + float2(0.5f, 0.5f);

	sp += gSHD[1] * 0.5f;

	float fShadow;

	float kr = gSHD[0] * KERNEL_RADIUS;
	float dx = rsqrt(1.0 - dLN*dLN);
	float ofs = kr / (dLN * dx);
	float omx = min(5e-3 + ofs, 0.15);

	//float krd = omx * dLN / dx;
	//float fSh = saturate( (krd / kr) * (1 - SHADOW_THRESHOLD) + SHADOW_THRESHOLD);

	if (gBaseBuilding) {

		// It's a surface base building
		fShadow = ProjectShadows(sp);
	}
	else {

		// It's a vessel
		float  pd = shdH.z - omx * gSHD[3];
		fShadow = SampleShadowsEx(sp, pd, sc);
		fShadow = smoothstep(0.0f, 1.0f, fShadow);
	}

	return sqrt(fShadow);
}