// vk_D3DUtils_stubs.cpp
// No-op implementations of all CDrawUtilities virtual methods for the VK build.
// D3DUtils.cpp is excluded (it uses DX9 primitive buffers); these stubs satisfy
// the vtable so CDrawUtilities DUImpl can be instantiated.
#include "stdafx.h"
#include "../xrRender/D3DUtils.h"

void SPrimitiveBuffer::CreateFromData(D3DPRIMITIVETYPE, u32, u32, LPVOID, u32, u16*, u32) {}
void SPrimitiveBuffer::Destroy() {}

void CDrawUtilities::OnDeviceCreate() {}
void CDrawUtilities::OnDeviceDestroy() {}
void CDrawUtilities::UpdateGrid(int, float, int) {}

void CDrawUtilities::DrawCross(const Fvector&, float, float, float, float, float, float, u32, BOOL) {}
void CDrawUtilities::DrawEntity(u32, ref_shader) {}
void CDrawUtilities::DrawFlag(const Fvector&, float, float, float, float, u32, BOOL) {}
void CDrawUtilities::DrawRomboid(const Fvector&, float, u32) {}
void CDrawUtilities::DrawJoint(const Fvector&, float, u32) {}
void CDrawUtilities::DrawSpotLight(const Fvector&, const Fvector&, float, float, u32) {}
void CDrawUtilities::DrawDirectionalLight(const Fvector&, const Fvector&, float, float, u32) {}
void CDrawUtilities::DrawPointLight(const Fvector&, float, u32) {}
void CDrawUtilities::DrawSound(const Fvector&, float, u32) {}
void CDrawUtilities::DrawLineSphere(const Fvector&, float, u32, BOOL) {}
void CDrawUtilities::dbgDrawPlacement(const Fvector&, int, u32, LPCSTR, u32) {}
void CDrawUtilities::dbgDrawVert(const Fvector&, u32, LPCSTR) {}
void CDrawUtilities::dbgDrawEdge(const Fvector&, const Fvector&, u32, LPCSTR) {}
void CDrawUtilities::dbgDrawFace(const Fvector&, const Fvector&, const Fvector&, u32, LPCSTR) {}
void CDrawUtilities::DrawFace(const Fvector&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawLine(const Fvector&, const Fvector&, u32) {}
void CDrawUtilities::DrawLink(const Fvector&, const Fvector&, float, u32) {}
void CDrawUtilities::DrawSelectionBox(const Fvector&, const Fvector&, u32*) {}
void CDrawUtilities::DrawIdentSphere(BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawIdentSpherePart(BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawIdentCone(BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawIdentCylinder(BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawIdentBox(BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawBox(const Fvector&, const Fvector&, BOOL, BOOL, u32, u32) {}
void CDrawUtilities::DrawAABB(const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawAABB(const Fmatrix&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawOBB(const Fmatrix&, const Fobb&, u32, u32) {}
void CDrawUtilities::DrawSphere(const Fmatrix&, const Fvector&, float, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawCylinder(const Fmatrix&, const Fvector&, const Fvector&, float, float, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawCone(const Fmatrix&, const Fvector&, const Fvector&, float, float, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawPlane(const Fvector&, const Fvector2&, const Fvector&, u32, u32, BOOL, BOOL, BOOL) {}
void CDrawUtilities::DrawPlane(const Fvector&, const Fvector&, const Fvector2&, u32, u32, BOOL, BOOL, BOOL) {}
void CDrawUtilities::DrawRectangle(const Fvector&, const Fvector&, const Fvector&, u32, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawGrid() {}
void CDrawUtilities::DrawPivot(const Fvector&, float) {}
void CDrawUtilities::DrawAxis(const Fmatrix&) {}
void CDrawUtilities::DrawObjectAxis(const Fmatrix&, float, BOOL) {}
void CDrawUtilities::DrawSelectionRect(const Ivector2&, const Ivector2&) {}
void CDrawUtilities::DrawPrimitiveL(D3DPRIMITIVETYPE, u32, Fvector*, int, u32, BOOL, BOOL) {}
void CDrawUtilities::DrawPrimitiveTL(D3DPRIMITIVETYPE, u32, FVF::TL*, int, BOOL, BOOL) {}
void CDrawUtilities::DrawPrimitiveLIT(D3DPRIMITIVETYPE, u32, FVF::LIT*, int, BOOL, BOOL) {}
void CDrawUtilities::OutText(const Fvector&, LPCSTR, u32, u32) {}
void CDrawUtilities::OnRender() {}

void CDrawUtilities::DD_DrawFace_begin(BOOL) {}
void CDrawUtilities::DD_DrawFace_push(const Fvector&, const Fvector&, const Fvector&, u32) {}
void CDrawUtilities::DD_DrawFace_end() {}
