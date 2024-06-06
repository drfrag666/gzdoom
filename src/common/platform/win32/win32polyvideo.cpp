
#include <assert.h>
#include "hardware.h"
#include "engineerrors.h"
#include "v_text.h"
#include <Windows.h>
#include "i_mainwindow.h"

EXTERN_CVAR(Bool, vid_vsync)

bool ViewportLinearScale();

#include <d3d9.h>
#pragma comment(lib, "d3d9.lib")

#ifndef D3DPRESENT_FORCEIMMEDIATE
#define D3DPRESENT_FORCEIMMEDIATE	0x00000100L // MinGW
#endif

bool d3davailable = true;

CVAR (Bool, vid_forcegdi, false, CVAR_ARCHIVE | CVAR_GLOBALCONFIG)

namespace
{
	int SrcWidth = 0;
	int SrcHeight = 0;
	int ClientWidth = 0;
	int ClientHeight = 0;
	bool CurrentVSync = false;

	HMODULE D3D9_dll;
	bool d3dexavailable = true;
	IDirect3D9 *d3d9 = nullptr;
	IDirect3DDevice9 *device = nullptr;
	IDirect3D9Ex *d3d9ex = nullptr;
	IDirect3DDevice9Ex *deviceex = nullptr;
	IDirect3DSurface9* surface = nullptr;
}

void I_PolyPresentInit()
{
	if (vid_forcegdi)
	{
		d3davailable = false;
		d3dexavailable = false;
		return;
	}

	// Load the Direct3D 9 library.
	if ((D3D9_dll = LoadLibrary (L"d3d9.dll")) == NULL)
	{
		I_FatalError("Unable to load d3d9.dll!\n");
	}

	// Obtain an IDirect3DEx interface.
	typedef HRESULT (WINAPI *DIRECT3DCREATE9EXFUNC)(UINT, IDirect3D9Ex**);
	DIRECT3DCREATE9EXFUNC direct3d_create_9Ex = (DIRECT3DCREATE9EXFUNC)GetProcAddress(D3D9_dll, "Direct3DCreate9Ex");
	if (!direct3d_create_9Ex)
	{
		d3dexavailable = false;
		Printf(TEXTCOLOR_RED "Direct3DCreate9Ex failed.\n");
	}

	if (d3dexavailable)
	{
		(*direct3d_create_9Ex)(D3D_SDK_VERSION, &d3d9ex);
		if (!d3d9ex)
		{
			d3dexavailable = false;
			Printf(TEXTCOLOR_RED "Direct3DCreate9Ex failed.\n");
		}
	}

	if (!d3dexavailable)
	{
		d3d9 = Direct3DCreate9(D3D_SDK_VERSION);
		if (!d3d9)
		{
			FreeLibrary (D3D9_dll);
			d3davailable = false;
			Printf(TEXTCOLOR_RED "Direct3DCreate9 failed. Falling back to GDI...\n");
		}
	}

	if (!d3davailable)
	{
		return;
	}

	RECT rect = {};
	GetClientRect(mainwindow.GetHandle(), &rect);

	ClientWidth = rect.right;
	ClientHeight = rect.bottom;

	D3DPRESENT_PARAMETERS pp = {};
	pp.Windowed = true;
	pp.SwapEffect = d3dexavailable? D3DSWAPEFFECT_FLIPEX : D3DSWAPEFFECT_DISCARD;
	pp.BackBufferWidth = ClientWidth;
	pp.BackBufferHeight = ClientHeight;
	pp.BackBufferCount = 1;
	pp.hDeviceWindow = mainwindow.GetHandle();
	pp.PresentationInterval = CurrentVSync ? (d3dexavailable ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_ONE) : D3DPRESENT_INTERVAL_IMMEDIATE;

	HRESULT result;
	if (d3dexavailable)
	{
		result = d3d9ex->CreateDeviceEx(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mainwindow.GetHandle(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, nullptr, &deviceex);
	}
	else
	{
		result = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mainwindow.GetHandle(), D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &device);
		if (FAILED(result) && (result != D3DERR_DEVICELOST || device == NULL))
			result = d3d9->CreateDevice(D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, mainwindow.GetHandle(), D3DCREATE_SOFTWARE_VERTEXPROCESSING, &pp, &device);
	}
	if (FAILED(result))
	{
		FreeLibrary (D3D9_dll);
		d3davailable = false;
		Printf(TEXTCOLOR_RED "IDirect3D9.CreateDevice failed. Falling back to GDI...\n");
		return;
	}
}

uint8_t *I_PolyPresentLock(int w, int h, bool vsync, int &pitch)
{
	HRESULT result;

	RECT rect = {};
	GetClientRect(mainwindow.GetHandle(), &rect);
	if (rect.right != ClientWidth || rect.bottom != ClientHeight || CurrentVSync != vsync)
	{
		if (surface)
		{
			surface->Release();
			surface = nullptr;
		}

		CurrentVSync = vsync;
		ClientWidth = rect.right;
		ClientHeight = rect.bottom;

		D3DPRESENT_PARAMETERS pp = {};
		pp.Windowed = true;
		pp.SwapEffect = d3dexavailable ? D3DSWAPEFFECT_FLIPEX : D3DSWAPEFFECT_DISCARD;
		pp.BackBufferWidth = ClientWidth;
		pp.BackBufferHeight = ClientHeight;
		pp.BackBufferCount = 1;
		pp.hDeviceWindow = mainwindow.GetHandle();
		pp.PresentationInterval = CurrentVSync ? (d3dexavailable ? D3DPRESENT_INTERVAL_DEFAULT : D3DPRESENT_INTERVAL_ONE) : D3DPRESENT_INTERVAL_IMMEDIATE;
		d3dexavailable ? deviceex->Reset(&pp) : device->Reset(&pp);
	}

	if (SrcWidth != w || SrcHeight != h || !surface)
	{
		if (surface)
		{
			surface->Release();
			surface = nullptr;
		}

		SrcWidth = w;
		SrcHeight = h;
		result = d3dexavailable ? deviceex->CreateOffscreenPlainSurface(SrcWidth, SrcHeight, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &surface, 0)
			: device->CreateOffscreenPlainSurface(SrcWidth, SrcHeight, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &surface, 0);
		if (FAILED(result))
		{
			I_FatalError("IDirect3DDevice9.CreateOffscreenPlainSurface failed");
		}
	}

	D3DLOCKED_RECT lockrect = {};
	result = surface->LockRect(&lockrect, nullptr, D3DLOCK_DISCARD);
	if (FAILED(result))
	{
		pitch = 0;
		return nullptr;
	}

	pitch = lockrect.Pitch;
	return (uint8_t*)lockrect.pBits;
}

void I_PolyPresentUnlock(int x, int y, int width, int height)
{
	surface->UnlockRect();

	IDirect3DSurface9 *backbuffer = nullptr;
	HRESULT result = d3dexavailable ? deviceex->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)
		: device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
	if (FAILED(result))
		return;

	result = d3dexavailable ? deviceex->BeginScene() : device->BeginScene();
	if (SUCCEEDED(result))
	{
		int count = 0;
		D3DRECT clearrects[4];
		if (y > 0)
		{
			clearrects[count].x1 = 0;
			clearrects[count].y1 = 0;
			clearrects[count].x2 = ClientWidth;
			clearrects[count].y2 = y;
			count++;
		}
		if (y + height < ClientHeight)
		{
			clearrects[count].x1 = 0;
			clearrects[count].y1 = y + height;
			clearrects[count].x2 = ClientWidth;
			clearrects[count].y2 = ClientHeight;
			count++;
		}
		if (x > 0)
		{
			clearrects[count].x1 = 0;
			clearrects[count].y1 = y;
			clearrects[count].x2 = x;
			clearrects[count].y2 = y + height;
			count++;
		}
		if (x + width < ClientWidth)
		{
			clearrects[count].x1 = x + width;
			clearrects[count].y1 = y;
			clearrects[count].x2 = ClientWidth;
			clearrects[count].y2 = y + height;
			count++;
		}
		if (count > 0)
			d3dexavailable ? deviceex->Clear(count, clearrects, D3DCLEAR_TARGET, 0, 0.0f, 0)
				: device->Clear(count, clearrects, D3DCLEAR_TARGET, 0, 0.0f, 0);

		RECT srcrect = {}, dstrect = {};
		srcrect.right = SrcWidth;
		srcrect.bottom = SrcHeight;
		dstrect.left = x;
		dstrect.top = y;
		dstrect.right = x + width;
		dstrect.bottom = y + height;
		if (ViewportLinearScale())
			d3dexavailable ? deviceex->StretchRect(surface, &srcrect, backbuffer, &dstrect, D3DTEXF_LINEAR)
				: device->StretchRect(surface, &srcrect, backbuffer, &dstrect, D3DTEXF_LINEAR);
		else
			d3dexavailable ? deviceex->StretchRect(surface, &srcrect, backbuffer, &dstrect, D3DTEXF_POINT)
				: device->StretchRect(surface, &srcrect, backbuffer, &dstrect, D3DTEXF_POINT);

		result = d3dexavailable ? deviceex->EndScene() : device->EndScene();
		if (SUCCEEDED(result))
			d3dexavailable ? deviceex->PresentEx(nullptr, nullptr, 0, nullptr, CurrentVSync ? 0 : D3DPRESENT_FORCEIMMEDIATE)
				: device->Present(nullptr, nullptr, 0, nullptr);
	}

	backbuffer->Release();
}

void I_PolyPresentDeinit()
{
	if (surface) surface->Release();
	if (deviceex) deviceex->Release();
	if (device) device->Release();
	if (d3d9ex) d3d9ex->Release();
	if (d3d9) d3d9->Release();
}

void I_PresentPolyImage(int w, int h, const void *pixels)
{
	BITMAPV5HEADER info = {};
	info.bV5Size = sizeof(BITMAPV5HEADER);
	info.bV5Width = w;
	info.bV5Height = -h;
	info.bV5Planes = 1;
	info.bV5BitCount = 32;
	info.bV5Compression = BI_RGB;
	info.bV5SizeImage = 0;
	info.bV5CSType = LCS_WINDOWS_COLOR_SPACE;

	RECT box = {};
	GetClientRect(mainwindow.GetHandle(), &box);

	HDC dc = GetDC(mainwindow.GetHandle());
	if (box.right == w && box.bottom == h)
		SetDIBitsToDevice(dc, 0, 0, w, h, 0, 0, 0, h, pixels, (const BITMAPINFO *)&info, DIB_RGB_COLORS);
	else
		StretchDIBits(dc, 0, 0, box.right, box.bottom, 0, 0, w, h, pixels, (const BITMAPINFO *)&info, DIB_RGB_COLORS, SRCCOPY);
	ReleaseDC(mainwindow.GetHandle(), dc);
}
