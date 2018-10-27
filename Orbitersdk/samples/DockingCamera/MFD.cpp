// =================================================================================================================================
//
// Copyright (C) 2018 Jarmo Nikkanen
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation 
// files (the "Software"), to use, copy, modify, merge, publish, distribute, interact with the Software and sublicense
// copies of the Software, subject to the following conditions:
//
// a) You do not sell, rent or auction the Software.
// b) You do not collect distribution fees.
// c) You do not remove or alter any copyright notices contained within the Software.
// d) This copyright notice must be included in all copies or substantial portions of the Software.
//
// If the Software is distributed in an object code form then in addition to conditions above:
// e) It must inform that the source code is available and how to obtain it.
// f) It must display "NO WARRANTY" and "DISCLAIMER OF LIABILITY" statements on behalf of all contributors like the one below.
//
// The accompanying materials such as artwork, if any, are provided under the terms of this license unless otherwise noted. 
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
// OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR
// IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
// =================================================================================================================================

#define STRICT
#define ORBITER_MODULE

#include "windows.h"
#include "orbitersdk.h"
#include "MFD.h"
#include "gcAPI.h"
#include "Sketchpad2.h"
#include "Shell.h"

// ============================================================================================================
// Global variables

int g_MFDmode; // identifier for new MFD mode


class GenericModule : public oapi::Module
{

public:

	GenericModule(HINSTANCE hInst) : Module(hInst) {	}

	~GenericModule() {	}

	void clbkSimulationStart(RenderMode rm) {	}

	void clbkSimulationEnd() {	}

	void clbkFocusChanged(OBJHANDLE hNew, OBJHANDLE hOld)
	{
	
		VESSEL *o = NULL;
		if (hOld) o = oapiGetVesselInterface(hOld);

		for (int i = 0; i < mfdLast; i++) {
			if (MFDList[i].hTrue) {
				if (o) if (MFDList[i].hVessel == o) MFDList[i].hTrue->FocusChanged(false);
			}
		}
	}
};


// ============================================================================================================
// API interface

DLLCLBK void InitModule (HINSTANCE hDLL)
{
	GenericModule *pFly = new GenericModule(hDLL);
	oapiRegisterModule(pFly);

	ShellMFD::InitModule(hDLL);

	static char *name = "Generic Camera";   // MFD mode name
	MFDMODESPECEX spec;
	spec.name = name;
	spec.key = OAPI_KEY_C;                // MFD mode selection key
	spec.context = NULL;
	spec.msgproc = ShellMFD::MsgProc;  // MFD mode callback function

	// Register the new MFD mode with Orbiter
	g_MFDmode = oapiRegisterMFDMode (spec);
}

// ============================================================================================================
//
DLLCLBK void ExitModule (HINSTANCE hDLL)
{
	// Unregister the custom MFD mode when the module is unloaded
	oapiUnregisterMFDMode (g_MFDmode);
	ShellMFD::ExitModule(hDLL);
}











// ============================================================================================================
// MFD class implementation

CameraMFD::CameraMFD(DWORD w, DWORD h, VESSEL *vessel)
	: MFD2 (w, h, vessel)
	, hVessel(vessel)
	, hFocus(vessel)
	, hRenderSrf(NULL)
	, hTexture(NULL)
	, hCamera(NULL)
	, hDock(NULL)
	, hAttach(NULL)
	, type(Type::Dock)
	, index(0)
	, bParent(false)
	, bNightVis(false)
	, bCross(true)
	, fov(30.0)	  // fov (i.e. Aparture) which is 1/2 of the vertical fov see oapiCameraAperture()
	, offset(0.0) // -0.2 "looks" better ?!
{

	font = oapiCreateFont (w/20, true, "Arial", (FontStyle)(FONT_BOLD | FONT_ITALIC), 450);
	
	if (gcInitialize()) {

		hTexture = oapiLoadTexture("samples/Crosshairs.dds");

		// Create 3D render target
		hRenderSrf = oapiCreateSurfaceEx(w, h, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE |
		                                       OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);

		// Clear the surface
		oapiClearSurface(hRenderSrf);	
	}

	SelectVessel(hVessel, Type::Dock);

	//oapiWriteLogV("CTR-Starting custom camera for vessel 0x%X", pV);
}


// ============================================================================================================
//
CameraMFD::~CameraMFD()
{
	oapiReleaseFont(font);

	// Attention, Always delete the camera before the surface !!!
	if (hCamera) gcDeleteCustomCamera(hCamera);
	if (hRenderSrf) oapiDestroySurface(hRenderSrf);
	if (hTexture) oapiReleaseTexture(hTexture);
}


// ============================================================================================================
//
void CameraMFD::FocusChanged(bool bGained)
{
	//if (bGained) oapiWriteLogV("Starting custom camera for vessel 0x%X", pV);
	//else oapiWriteLogV("Shutting down custom camera for vessel 0x%X", pV);

	if (gcEnabled()) {

		// Always delete the camera first
		if (hCamera) gcDeleteCustomCamera(hCamera);
		if (hRenderSrf) oapiDestroySurface(hRenderSrf);

		hCamera = NULL;
		hRenderSrf = NULL;

		if (bGained) {

			// Create 3D render target
			hRenderSrf = oapiCreateSurfaceEx(W, H, OAPISURFACE_RENDER3D | OAPISURFACE_TEXTURE |
			                                       OAPISURFACE_RENDERTARGET | OAPISURFACE_NOMIPMAPS);

			// Clear the surface
			oapiClearSurface(hRenderSrf);

			SelectVessel(hVessel, Type::Dock);
		}
	}
}


// ============================================================================================================
//
void CameraMFD::UpdateDimensions(DWORD w, DWORD h)
{
	W = w; H = h;
	FocusChanged(true);	
}


// ============================================================================================================
//
void CameraMFD::SelectVessel(VESSEL *hVes, Type _type)
{
	VECTOR3 pos, dir, rot;

	pos = _V(0, 0, 0);
	dir = _V(1, 0, 0);
	rot = _V(0, 1, 0);

	type = _type;

	if (hVes != hVessel) {
		// New Vessel Selected
		offset = 0.0; // -0.2 "looks" better ?!
		index = 0;
		type = Type::Dock;
	}

	hVessel = hVes;



	int nDock = hVessel->DockCount();
	int nAtch = hVessel->AttachmentCount(bParent);

	if (nDock == 0 && nAtch == 0) return;

	if (type == Type::Atch && nAtch == 0) type = Type::Dock;
	if (type == Type::Dock && nDock == 0) type = Type::Atch;

	if (fov < 5.0) fov = 5.0;
	if (fov > 70.0) fov = 70.0;

	// Attachemnts
	if (type == Type::Atch) {

		if (index < 0) index = nAtch - 1;
		if (index >= nAtch) index = 0;

		hAttach = hVessel->GetAttachmentHandle(bParent, index);

		if (hAttach) {
			hVessel->GetAttachmentParams(hAttach, pos, dir, rot);
			pos += dir * offset;
		}
		else return;
	}

	// Docking ports
	if (type == Type::Dock) {

		if (index < 0) index = nDock - 1;
		if (index >= nDock) index = 0;

		hDock = hVessel->GetDockHandle(index);

		if (hDock) {
			hVessel->GetDockParams(hDock, pos, dir, rot);
			pos += dir * offset;
		}
		else return;
	}

	// Actual rendering of the camera view into hRenderSrf will occur when the client is ready for it and 
	// a lagg of a few frames may occur depending about graphics/performance options.
	// Update will continue untill the camera is turned off via ogciCustomCameraOnOff() or deleted via ogciDeleteCustomCamera()
	// Camera orientation can be changed by calling this function again with an existing camera handle instead of NULL.

	hCamera = gcSetupCustomCamera(hCamera, hVessel->GetHandle(), pos, dir, rot, fov*PI / 180.0, hRenderSrf, 0xFF);
}


// ============================================================================================================
//
char *CameraMFD::ButtonLabel (int bt)
{
	// The labels for the two buttons used by our MFD mode
	static char *label[] = {"NA", "PA", "ND", "PD", "FWD", "BWD", "VES", "NV", "ZM+", "ZM-", "PAR", "CRS"};
	return (bt < ARRAYSIZE(label) ? label[bt] : 0);
}


// ============================================================================================================
//
int CameraMFD::ButtonMenu (const MFDBUTTONMENU **menu) const
{
	// The menu descriptions for the two buttons
	static const MFDBUTTONMENU mnu[] = {
		{ "Next attachment", 0, '1' },
		{ "Prev attachment", 0, '2' },
		{ "Next dockport", 0, '3' },
		{ "Prev dockport", 0, '4' },
		{ "Move Forward", 0, '5' },
		{ "Move Backwards", 0, '6' },
		{ "Select Vessel", 0, '7' },
		{ "Night Vision", 0, '8' },
		{ "Zoom In", 0, '9' },
		{ "Zoom Out", 0, '0' },
		{ "Parent Mode", 0, 'B' },
		{ "Cross-hairs", 0, 'C' }
	};

	if (menu) *menu = mnu;

	return ARRAYSIZE(mnu); // return the number of buttons used
}


// ============================================================================================================
//
bool CameraMFD::Update(oapi::Sketchpad *skp)
{
	
	hShell->InvalidateDisplay();
	
	// Call to update attachments
	SelectVessel(hVessel, type);

	int nDock = hVessel->DockCount();
	int nAtch = hVessel->AttachmentCount(bParent);

	RECT /*ch,*/ sr, clip;
	
	sr.left = 0; sr.top = 0;
	sr.right = W - 2; sr.bottom = H - 2;

	clip.left = 25; clip.top = 25;
	clip.right = W - 27; clip.bottom = H - 27;

	if (hRenderSrf && gcSketchpadVersion(skp) == 2) {

		oapi::Sketchpad3 *pSkp3 = (oapi::Sketchpad3 *)skp;


		if (bNightVis) {
			pSkp3->SetBrightness(&_FVECTOR4(0.0, 4.0, 0.0, 1));
			pSkp3->SetRenderParam(SKP3_PRM_GAMMA, &_FVECTOR4(0.5f, 0.5f, 0.5f, 1.0f));
			pSkp3->SetRenderParam(SKP3_PRM_NOISE, &_FVECTOR4(0.0f, 0.3f, 0.0f, 0.0f));
		}


		// Blit the camera view into the sketchpad.
		pSkp3->CopyRect(hRenderSrf, &sr, 1, 1);


		if (bNightVis) {
			pSkp3->SetBrightness(NULL);
			pSkp3->SetRenderParam(SKP3_PRM_GAMMA, NULL);
			pSkp3->SetRenderParam(SKP3_PRM_NOISE, NULL);
		}


		// Draw the cross-hairs
		if (bCross) {

			pSkp3->ClipRect(&clip);

			IVECTOR2 rc;
			rc.x = W / 2;
			rc.y = H / 2;

			int y = H / 2 - 2;
			int x = W / 2;

			//ch = { 0, 0, 255, 4 };
			//pSkp3->CopyRect(hTexture, &ch, x - 256, y);
			//ch = { 255, 0, 0, 4 };
			//pSkp3->CopyRect(hTexture, &ch, x - 1, y);
			pSkp3->CopyRect(hTexture, NULL, x - 254, y);
			pSkp3->CopyRect(hTexture, NULL, x + 2, y);

			pSkp3->SetWorldTransform2D(1.0f, float(PI05), &rc, NULL);

			pSkp3->CopyRect(hTexture, NULL, x - 254, y);
			pSkp3->CopyRect(hTexture, NULL, x + 2, y);
			//y = H / 2;
			//ch = { 0, 0, 255, 4 };
			//pSkp3->CopyRect(hTexture, &ch, x - 256, y);
			//ch = { 255, 0, 0, 4 };
			//pSkp3->CopyRect(hTexture, &ch, x, y);

			// Back to defaults
			pSkp3->SetWorldTransform();
			pSkp3->ClipRect(NULL);
		}
	}
	else {
		static char *msg = { "No Graphics API" };
		skp->SetTextAlign(Sketchpad::TAlign_horizontal::CENTER);
		skp->Text(W / 2, H / 2, msg, strlen(msg));
		return true;
	}
	
	if (!hCamera) {
		static char *msg = { "Custom Cameras Disabled" };
		skp->SetTextAlign(Sketchpad::TAlign_horizontal::CENTER);
		skp->Text(W / 2, H / 2, msg, strlen(msg));
		return true;
	}

	if (nDock == 0 && nAtch == 0) {
		static char *msg = { "No Dock/Attachment points" };
		skp->SetTextAlign(Sketchpad::TAlign_horizontal::CENTER);
		skp->Text(W / 2, H / 2, msg, strlen(msg));
		return true;
	}


	skp->SetTextAlign();

	char text[256];
	static const char* mode[] = { "Attach(", "Dock(" };
	static const char* paci[] = { "Child", "Parent" };

	sprintf_s(text, 256, "Viewing %s %s%d)", hVessel->GetName(), mode[type], index);

	if (gcSketchpadVersion(skp) == 2) {
		oapi::Sketchpad2 *pSkp2 = (oapi::Sketchpad2 *)skp;
		pSkp2->QuickBrush(0xA0000000);
		pSkp2->QuickPen(0);
		pSkp2->Rectangle(1, 1, W - 1, 25);
		pSkp2->Rectangle(1, H - 25, W - 1, H - 1);
	}

	hShell->Title (skp, text);

	sprintf_s(text, 256, "[%s] FOV=%0.0f� Ofs=%2.2f[m]", paci[bParent], fov*2.0, offset);

	skp->Text(10, H - 25, text, strlen(text));
	
	return true;
}


// ============================================================================================================
//
bool CameraMFD::ConsumeKeyBuffered(DWORD key)
{
	
	switch (key) {


	case OAPI_KEY_1:	// Next Attachment
		index++;
		SelectVessel(hVessel, Type::Atch);
		return true;


	case OAPI_KEY_2:	// Prev Attachment
		index--;
		SelectVessel(hVessel, Type::Atch);
		return true;


	case OAPI_KEY_3:	// Next dock
		index++;
		SelectVessel(hVessel, Type::Dock);
		return true;


	case OAPI_KEY_4:	// Prev dock
		index--;
		SelectVessel(hVessel, Type::Dock);
		return true;


	case OAPI_KEY_5:	// Move forward
		offset += 0.1;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_6:	// Move backwards
		offset -= 0.1;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_7:	// Select Vessel
		oapiOpenInputBox("Keyboard Input:", DataInput, 0, 32, (void*)this);
		return true;


	case OAPI_KEY_8:	// Night vision toggle
		bNightVis = !bNightVis;
		return true;


	case OAPI_KEY_9:	// Zoom in
		fov -= 5.0;
		SelectVessel(hVessel, type);
		return true;


	case OAPI_KEY_0:	// Zoom out
		fov += 5.0;
		SelectVessel(hVessel, type);
		return true;

	case OAPI_KEY_B:	// Parent/Child
		bParent = !bParent;
		SelectVessel(hVessel, type);
		return true;

	case OAPI_KEY_C:	// Crosshairs
		bCross = !bCross;
		return true;

	}

	return false;
}


// ============================================================================================================
//
bool CameraMFD::ConsumeButton(int bt, int event)
{
	static const DWORD btkey[12] = { OAPI_KEY_1, OAPI_KEY_2, OAPI_KEY_3, OAPI_KEY_4, OAPI_KEY_5, OAPI_KEY_6,
		OAPI_KEY_7, OAPI_KEY_8, OAPI_KEY_9, OAPI_KEY_0, OAPI_KEY_B, OAPI_KEY_C };

	if (event&PANEL_MOUSE_LBDOWN) {					
		return ConsumeKeyBuffered(btkey[bt]);
	}

	return false;
}


// ============================================================================================================
//
bool CameraMFD::DataInput(void *id, char *str)
{
	OBJHANDLE hObj = oapiGetVesselByName(str);

	if (hObj) {
		SelectVessel(oapiGetVesselInterface(hObj), type);
		return true;
	}

	return false;
}


// ============================================================================================================
//
bool CameraMFD::DataInput(void *id, char *str, void *data)
{
	return ((CameraMFD*)data)->DataInput(id, str);
}


// ============================================================================================================
//
void CameraMFD::WriteStatus(FILEHANDLE scn) const
{

}


// ============================================================================================================
//
void CameraMFD::ReadStatus(FILEHANDLE scn)
{
	
}
