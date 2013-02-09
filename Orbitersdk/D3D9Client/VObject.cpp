// ==============================================================
// VObject.cpp
// Part of the ORBITER VISUALISATION PROJECT (OVP)
// Released under GNU General Public License
// Copyright (C) 2006 Martin Schweiger
//				 2010-2012 Jarmo Nikkanen (D3D9Client related parts)
// ==============================================================

// ==============================================================
// class vObject (implementation)
//
// A "vObject" is the visual representation of an Orbiter object
// (vessel, planet/moon/sun, surface base). vObjects usually have
// one or more meshes associated with it that define their visual
// appearance, but they can be arbitrarily complex (e.g. planets
// with clould layers, atmospheric haze, etc.)
// Visual objects don't persist as their "logical" counterparts,
// but are created and deleted as they pass in and out of the
// visual range of a camera. vObjects are therefore associated
// with a particular scene. In multi-scene environments, a single
// logical object may have multiple vObjects associated with it.
// ==============================================================

#include "VObject.h"
#include "VVessel.h"
#include "VPlanet.h"
#include "VStar.h"
#include "VBase.h"
#include "Texture.h"
#include "D3D9Util.h"
#include "Mesh.h"
#include "D3D9Surface.h"
#include "D3D9Client.h"

using namespace oapi;

// Initialisation of static members

D3D9Client *vObject::gc = NULL;
D3D9ClientSurface *vObject::blobtex[3] = {0,0,0};

// Constructor
vObject::vObject(OBJHANDLE _hObj, const Scene *scene): VisObject (_hObj)
{
	_TRACE;
	active = true;
	bBSRecompute = true;
	hObj = _hObj;
	scn  = scene;
	D3DXMatrixIdentity(&mWorld);
	cdist = 0.0;
	albedo = _V(1,1,1);
	oapiGetObjectName(hObj, name, 64);
}

vObject *vObject::Create(OBJHANDLE _hObj, const Scene *scene)
{
	_TRACE;

	int objtp = oapiGetObjectType(_hObj);
	switch (objtp) {
		case OBJTP_VESSEL:	    return new vVessel (_hObj, scene); 
		case OBJTP_PLANET:		return new vPlanet (_hObj, scene); 
		case OBJTP_STAR:		return new vStar   (_hObj, scene); 
		case OBJTP_SURFBASE:	return new vBase   (_hObj, scene); 
		default:
		{
			LogErr("Unidentified Object Type %d in 0x%X",oapiGetObjectType(_hObj),_hObj);
			return new vObject (_hObj, scene);
		}		
	}
}

void vObject::GlobalInit(D3D9Client *gclient)
{
	_TRACE;
	static char *fname[3] = {"Ball.dds","Ball2.dds","Ball3.dds"};
	gc = gclient;
	for (int i=0;i<3;i++) blobtex[i] = SURFACE(gc->clbkLoadTexture(fname[i]));
}

void vObject::GlobalExit()
{
	_TRACE;
	for (int i=0;i<3;i++) SAFE_DELETE(blobtex[i]);
}

void vObject::Activate(bool isactive)
{
	active = isactive;
}

DWORD vObject::GetMeshCount()
{ 
	return 0;
}

bool vObject::Update()
{
	if (!active) return false;

	MATRIX3 grot;
	oapiGetRotationMatrix(hObj, &grot);
	oapiGetGlobalPos(hObj, &cpos);
	cpos -= scn->GetCameraGPos();

	// object positions are relative to camera

	cdist = length(cpos);

	// camera distance
	D3DMAT_SetInvRotation(&mWorld, &grot);
	D3DMAT_SetTranslation(&mWorld, &cpos);

	D3DXMatrixInverse(&mWorldInv, NULL, &mWorld);

	// update the object's world matrix
	CheckResolution();

	return true;
}

void vObject::UpdateBoundingBox()
{
	
}

VECTOR3 vObject::GetBoundingSpherePos()
{
	if (bBSRecompute) UpdateBoundingBox();
	D3DXVECTOR3 pos;
	D3DXVec3TransformCoord(&pos, (LPD3DXVECTOR3)&BBox.bs, &mWorld);
	return _V((double)pos.x, (double)pos.y, (double)pos.z);
}

float vObject::GetBoundingSphereRadius()
{
	if (bBSRecompute) UpdateBoundingBox();
	return BBox.bs.w;
}

const char *vObject::GetName()
{
	return name;
}


bool vObject::IsVisible()
{
	VECTOR3 pos  = GetBoundingSpherePos();
	float rad = GetBoundingSphereRadius();

	bool bVis = gc->GetScene()->IsVisibleInCamera(&D3DXVEC(pos), rad);

	if (bVis) {
		double brad = oapiGetSize(gc->GetScene()->GetCameraProxyBody());
		double crad = cdist;
		double alfa = acos(brad/crad);
		double trad = length(pos+cpos);
		double beta = acos(dotp(pos+cpos, cpos)/(crad*trad));

		if (beta<alfa) return true;

		double resl = brad - trad * cos(beta-alfa);
		
		if (resl>rad) return false;
	}

	return bVis;
}


// This routine will render beacons
//
void vObject::RenderSpot(LPDIRECT3DDEVICE9 dev, const VECTOR3 *ofs, float size, const VECTOR3 &col, bool lighting, int shape)
{
	MATRIX3 grot;
	VECTOR3 gpos, pos(cpos);

	oapiGetRotationMatrix(hObj, &grot);
	oapiGetGlobalPos(hObj, &gpos);

	if (ofs) pos += mul (grot, *ofs);
	VECTOR3 camp = scn->GetCameraGPos();

	const double ambient = 0.2;
	double cosa = dotp (unit(gpos), unit(gpos - camp));
	double intens = (lighting ? 0.5 * ((1.0-ambient)*cosa + 1.0+ambient) : 1.0);

	D3DXMATRIX W;
	D3DXVECTOR3 vPos(float(pos.x), float(pos.y), float(pos.z));
	D3DXVECTOR3 vCam;
	D3DXVec3Normalize(&vCam, &vPos);
	D3DMAT_CreateX_Billboard(&vCam, &vPos, size, &W);

	D3DXCOLOR color((float)col.x, (float)col.y, (float)col.z, 1.0f);

	D3D9Effect::RenderSpot((float)intens, &color, (const LPD3DXMATRIX)&W, blobtex[shape]);
}



// This routine is for rendering celestial body dots
//
void vObject::RenderDot(LPDIRECT3DDEVICE9 dev)
{
	if (hObj==NULL) return;
	
	VECTOR3 gpos,spos;
	oapiGetGlobalPos(hObj, &gpos);
	oapiGetGlobalPos(oapiGetGbodyByIndex(0), &spos);

	cpos = gpos - scn->GetCameraGPos();
	cdist = length(cpos);

	double rad = oapiGetSize(hObj);
	double alt = max(1.0, cdist-rad);
	double apr = rad * scn->ViewH()*0.5 / (alt * tan(scn->GetCameraAperture()));
	
	double ds = 10000.0 / cdist;
	double s = 2.0;
	if (apr<0.3) s = 1.0;
	float size = float(rad * ds * s/apr);

	D3DXMATRIX W;
	D3DXVECTOR3 vCam;
	D3DXVECTOR3 vPos(float(cpos.x), float(cpos.y), float(cpos.z));
	vPos*=float(ds);

	D3DXVec3Normalize(&vCam, &vPos);
	D3DMAT_CreateX_Billboard(&vCam, &vPos, size, &W);

	float ints = float(sqrt(1.0+dotp(unit(gpos-spos), unit(cpos)))) * 1.0f;

	if (ints>1.0f) ints=1.0f;
	
	D3DXCOLOR color(float(albedo.x)*ints, float(albedo.y)*ints, float(albedo.z)*ints, 1.0f);

	D3D9Effect::RenderSpot(1.0f, &color, (const LPD3DXMATRIX)&W, blobtex[0]);
}



void vObject::RenderAxisVector(Sketchpad *pSkp, LPD3DXCOLOR pColor, VECTOR3 vector, float lscale, float size, bool bLog)
{
	MATRIX3 grot;
	D3DXMATRIX W;
	VECTOR3 gpos, camp;

    oapiGetRotationMatrix(hObj, &grot);
	VECTOR3 dir = mul(grot, vector);

	oapiGetGlobalPos(hObj, &gpos);

	camp = gc->GetScene()->GetCameraGPos();

    VECTOR3 pos = gpos - camp;
	VECTOR3 rot = crossp(pos, vector);
   
    VECTOR3 y = mul (grot, unit(vector)) * size;
    VECTOR3 x = mul (grot, unit(rot)) * size;
    VECTOR3 z = mul (grot, unit(crossp(vector, rot))) * size;

    D3DXMatrixIdentity(&W);

    W._11 = float(x.x);
    W._12 = float(x.y);
    W._13 = float(x.z);

    W._21 = float(y.x);
    W._22 = float(y.y);
    W._23 = float(y.z);

    W._31 = float(z.x);
    W._32 = float(z.y);
    W._33 = float(z.z);

    W._41 = float(pos.x);
    W._42 = float(pos.y);
    W._43 = float(pos.z);

	float len = float(length(vector));
	
	if (bLog) len = max(0.0f, 13.0f+log(len)) * lscale / size;
	else      len = len * lscale / size;

	D3D9Effect::RenderAxisVector(&W, pColor, len);
}



void vObject::RenderAxisLabel(Sketchpad *pSkp, LPD3DXCOLOR clr, VECTOR3 vector, float lscale, float size, const char *label, bool bLog)
{
	D3DXVECTOR3 homog,ws;
	MATRIX3 grot;
	D3DXMATRIX W;
	VECTOR3 gpos, camp;

    oapiGetRotationMatrix(hObj, &grot);
	VECTOR3 dir = mul(grot, vector);

	oapiGetGlobalPos(hObj, &gpos);

    camp = gc->GetScene()->GetCameraGPos();

    VECTOR3 pos = gpos - camp;
	VECTOR3 rot = crossp(pos, vector);
   
    VECTOR3 y = mul (grot, unit(vector)) * size;
    VECTOR3 x = mul (grot, unit(rot)) * size;
    VECTOR3 z = mul (grot, unit(crossp(vector, rot))) * size;

    D3DXMatrixIdentity(&W);

    W._11 = float(x.x);
    W._12 = float(x.y);
    W._13 = float(x.z);

    W._21 = float(y.x);
    W._22 = float(y.y);
    W._23 = float(y.z);

    W._31 = float(z.x);
    W._32 = float(z.y);
    W._33 = float(z.z);

    W._41 = float(pos.x);
    W._42 = float(pos.y);
    W._43 = float(pos.z);

	float len = float(length(vector));
	
	if (bLog) len = max(0.0f, 13.0f+log(len)) * lscale / size;
	else      len = len * lscale / size;

	D3DXVec3TransformCoord(&ws, &D3DXVECTOR3(0,len,0), &W);
	D3DXVec3TransformCoord(&homog, &ws, scn->GetProjectionViewMatrix());

	if (D3DXVec3Dot(&ws, scn->GetCameraZ())<0) return;

	if (homog.x >= -1.0f && homog.x <= 1.0f && homog.y >= -1.0f && homog.y <= 1.0f) {
		int xc = (int)(scn->ViewW()*0.5*(1.0f+homog.x));
		int yc = (int)(scn->ViewH()*0.5*(1.0f-homog.y));
		pSkp->SetTextColor(D3DXCOLOR(clr->b, clr->g, clr->r, clr->a));
		pSkp->Text(xc+10, yc, label, strlen(label));
	}
}

