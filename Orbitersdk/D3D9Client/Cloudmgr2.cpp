// ==============================================================
//   ORBITER VISUALISATION PROJECT (OVP)
//   Copyright (C) 2006-2014 Martin Schweiger
//   Dual licensed under GPL v3 and LGPL v3
// ==============================================================

// ==============================================================
// cloudmgr2.cpp
// Rendering of planetary cloud layers, engine v2, including a simple
// LOD (level-of-detail) algorithm for cloud patch resolution.
// ==============================================================

#include "cloudmgr2.h"
#include "Texture.h"
#include "D3D9Catalog.h"
#include "D3D9Config.h"

// =======================================================================
// =======================================================================

CloudTile::CloudTile (TileManager2Base *_mgr, int _lvl, int _ilat, int _ilng)
: Tile (_mgr, _lvl, _ilat, _ilng)
{
	cmgr = static_cast<TileManager2<CloudTile>* > (_mgr);
	node = 0;
	mean_elev = mgr->GetPlanet()->prm.cloudalt;
}

// -----------------------------------------------------------------------

CloudTile::~CloudTile ()
{
}

// -----------------------------------------------------------------------

void CloudTile::PreLoad()
{
	char path[MAX_PATH] = {'\0'};

	// Load cloud texture
	sprintf_s (path, "%s\\Cloud\\%02d\\%06d\\%06d.dds", mgr->CbodyName(), lvl+4, ilat, ilng);
	bool found = mgr->GetClient()->TexturePath(path, path);
	if (found) LoadFile(path, &TexBuffer, &TexSize);
}


// -----------------------------------------------------------------------

void CloudTile::Load ()
{
	
	LPDIRECT3DDEVICE9 pDev = mgr->Dev();

	owntex = true;

	if (CreateTexture(pDev, TexBuffer, TexSize, &tex) != true) {
		if (GetParentSubTexRange (&texrange)) {
			tex = getParent()->Tex();
			owntex = false;
		} else tex = 0;
	} else {
		TileCatalog->Add(DWORD(tex));
		mgr->TileLabel(tex, lvl, ilat, ilng);
	}

	bool shift_origin = (lvl >= 4);

	if (!lvl) {
		// create hemisphere mesh for western or eastern hemispheres
		mesh = CreateMesh_hemisphere (TILE_PATCHRES, 0, mean_elev);
	//} else if (ilat == 0 || ilat == (1<<lvl)-1) {
		// create triangular patch for north/south pole region
	//	mesh = CreateMesh_tripatch (TILE_PATCHRES, elev, shift_origin, &vtxshift);
	} else {
		// create rectangular patch
		mesh = CreateMesh_quadpatch (TILE_PATCHRES, TILE_PATCHRES, 0, mean_elev, &texrange, shift_origin, &vtxshift);
	}
}

// -----------------------------------------------------------------------

void CloudTile::Render ()
{
	UINT numPasses = 0;

	LPDIRECT3DDEVICE9 pDev = mgr->Dev();
	ID3DXEffect *Shader = mgr->Shader();

	// ---------------------------------------------------------------------
	// Feed tile specific data to shaders
	//
	// ---------------------------------------------------------------------------------------------------
	HR(Shader->SetTexture(TileManager2Base::stDiff, tex));
	// ---------------------------------------------------------------------------------------------------

	Shader->CommitChanges();

	HR(Shader->Begin(&numPasses, D3DXFX_DONOTSAVESTATE));

	// -------------------------------------------------------------------
	// render surface mesh

	HR(Shader->BeginPass(0));
	pDev->SetVertexDeclaration(pPatchVertexDecl);
	pDev->SetStreamSource(0, mesh->pVB, 0, sizeof(VERTEX_2TEX));
	pDev->SetIndices(mesh->pIB);
	pDev->DrawIndexedPrimitive(D3DPT_TRIANGLELIST, 0, 0, mesh->nv, 0, mesh->nf);
	HR(Shader->EndPass());

	HR(Shader->End());
}


// =======================================================================
// =======================================================================

template<>
void TileManager2<CloudTile>::Render (MATRIX4 &dwmat, bool use_zbuf, const vPlanet::RenderPrm &rprm)
{
	// set generic parameters
	SetRenderPrm (dwmat, rprm.cloudrot, use_zbuf, rprm);

	double np = 0.0, fp = 0.0;
	int i;

	class Scene *scene = GetClient()->GetScene();

	// adjust scaling parameters (can only be done if no z-buffering is in use)
	if (!use_zbuf) {
		double R = obj_size;
		double Rc = R+rprm.cloudalt;
		double D = prm.cdist*R;
		double zmin, zmax;
		if (D > Rc) {
			zmax = sqrt(D*D-Rc*Rc);
			zmin = D-Rc;
		} else {
			zmax = sqrt(D*D-R*R) + sqrt(Rc*Rc-R*R);
			zmin = Rc-D;
		}
		zmin = max (2.0, min (zmax*1e-4, zmin));

		np = scene->GetCameraNearPlane();
		fp = scene->GetCameraFarPlane();
		scene->SetCameraFrustumLimits (zmin, zmax);
	}

	// build a transformation matrix for frustum testing
	MATRIX4 Mproj = _MATRIX4(scene->GetProjectionMatrix());
	//Mproj.m33 = 1.0; Mproj.m43 = -1.0;  // adjust near plane to 1, far plane to infinity
	MATRIX4 Mview = _MATRIX4(scene->GetViewMatrix());
	prm.dviewproj = mul(Mview,Mproj);

	// ---------------------------------------------------------------------
	// Initialize shading technique and feed planet specific data to shaders
	//
	HR(Shader()->SetTechnique(eCloudTech));
	HR(Shader()->SetMatrix(smViewProj, scene->GetProjectionViewMatrix()));
	HR(Shader()->SetVector(svTexOff,  &D3DXVECTOR4(0, 0, 0, 0)));
	HR(Shader()->SetVector(svGeneric, &D3DXVECTOR4(1, 1, 1, 1)));

	if (rprm.bCloudBrighten) { HR(Shader()->SetFloat(sfAlpha, 2.0f)); }
	else					 { HR(Shader()->SetFloat(sfAlpha, 1.0f)); }

	// TODO: render full sphere for levels < 4

	loader->WaitForMutex();

	// update the tree
	for (i = 0; i < 2; i++)
		ProcessNode (tiletree+i);

	// render the tree
	for (i = 0; i < 2; i++)
		RenderNode (tiletree+i);
	
	loader->ReleaseMutex ();

	if (np)
		scene->SetCameraFrustumLimits(np,fp);
}

// -----------------------------------------------------------------------

template<>
void TileManager2<CloudTile>::RenderFlatCloudShadows (MATRIX4 &dwmat, const vPlanet::RenderPrm &rprm)
{
	
	double scale = obj_size/(obj_size+GetPlanet()->prm.cloudalt);

	MATRIX4 dwmat_scaled = {
		dwmat.m11*scale, dwmat.m12*scale, dwmat.m13*scale, dwmat.m14*scale,
		dwmat.m21*scale, dwmat.m22*scale, dwmat.m23*scale, dwmat.m24*scale,
		dwmat.m31*scale, dwmat.m32*scale, dwmat.m33*scale, dwmat.m34*scale,
		dwmat.m41,       dwmat.m42,       dwmat.m43,       dwmat.m44};

	SetRenderPrm (dwmat_scaled, rprm.cloudrot, false, rprm);

	prm.grot *= scale;

	float alpha = (float)rprm.shadowalpha;
	if (alpha < 0.01f) return; // don't render cloud shadows for this planet

	class Scene *scene = GetClient()->GetScene();

	double np = 0.0, fp = 0.0;
	int i;

	double R = obj_size;
	double D = prm.cdist*R;
	double zmax = sqrt(D*D-R*R);
	double zmin = max (2.0, min (zmax*1e-4, (D-R)*0.8));
	np = scene->GetCameraNearPlane();
	fp = scene->GetCameraFarPlane();
	scene->SetCameraFrustumLimits (zmin, zmax);

	// build a transformation matrix for frustum testing
	MATRIX4 Mproj = _MATRIX4(scene->GetProjectionMatrix());
	//Mproj.m33 = 1.0; Mproj.m43 = -1.0;  // adjust near plane to 1, far plane to infinity
	MATRIX4 Mview = _MATRIX4(scene->GetViewMatrix());
	prm.dviewproj = mul(Mview, Mproj);

	// ---------------------------------------------------------------------
	// Initialize shading technique and feed planet specific data to shaders
	//
	HR(Shader()->SetTechnique(eCloudShadowTech));
	HR(Shader()->SetMatrix(smViewProj, scene->GetProjectionViewMatrix()));
	HR(Shader()->SetVector(svTexOff,  &D3DXVECTOR4(0, 0, 0, 0)));
	HR(Shader()->SetVector(svGeneric, &D3DXVECTOR4(1, 1, 1, 1)));
	HR(Shader()->SetFloat(sfAlpha, alpha));
	
	// TODO: render full sphere for levels < 4

	loader->WaitForMutex();

	// render the tree
	for (i = 0; i < 2; i++)
		RenderNode (tiletree+i);
	
	loader->ReleaseMutex ();

	if (np)
		scene->SetCameraFrustumLimits (np, fp);
}

// -----------------------------------------------------------------------

template<>
int TileManager2<CloudTile>::Coverage (double latmin, double latmax, double lngmin, double lngmax, int maxlvl, const Tile **tbuf, int nt) const
{
	double crot = GetPlanet()->prm.cloudrot;
	//double crot = prm.rprm->cloudrot;
	lngmin = lngmin + crot;
	lngmax = lngmax + crot;
	if (lngmin > PI) {
		lngmin -= PI2;
		lngmax -= PI2;
	}
	int nfound = 0;
	for (int i = 0; i < 2; i++) {
		CheckCoverage (tiletree+i, latmin, latmax, lngmin, lngmax, maxlvl, tbuf, nt, &nfound);
	}
	return nfound;
}
