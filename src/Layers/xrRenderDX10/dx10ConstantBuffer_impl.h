#ifndef	dx10ConstantBuffer_impl_included
#define	dx10ConstantBuffer_impl_included
#pragma once

IC Fvector4* dx10ConstantBuffer::Access(u16 offset)
{
#if defined(USE_VK)
const u32 workerId = g_vkWorkerId;
if (!m_pBufferData[workerId] || m_uiBufferSize == 0)
{
static thread_local Fvector4 s_dummy{0,0,0,0};
return &s_dummy;
}
VERIFY(offset < (int)m_uiBufferSize);
BYTE* res = ((BYTE*)m_pBufferData[workerId]) + offset;
return (Fvector4*)res;
#else
// We no longer set m_bChanged here. The set() functions will set it if memory differs.
VERIFY(offset<(int)m_uiBufferSize);
BYTE* res = ((BYTE*)m_pBufferData) + offset;
return (Fvector4*)res;
#endif
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fmatrix& A)
{
	VERIFY(RC_float == C->type);
	Fvector4* it = Access(L.index);
	
	bool bChanged = false;
	switch (L.cls)
	{
	case RC_2x4:
		VERIFY(u32((u32)L.index+2*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			bChanged = true;
		}
		break;
	case RC_3x4:
		VERIFY(u32((u32)L.index+3*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42 ||
			it[2].x != A._13 || it[2].y != A._23 || it[2].z != A._33 || it[2].w != A._43) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			it[2].set(A._13, A._23, A._33, A._43);
			bChanged = true;
		}
		break;
	case RC_4x4:
		VERIFY(u32((u32)L.index+4*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42 ||
			it[2].x != A._13 || it[2].y != A._23 || it[2].z != A._33 || it[2].w != A._43 ||
			it[3].x != A._14 || it[3].y != A._24 || it[3].z != A._34 || it[3].w != A._44) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			it[2].set(A._13, A._23, A._33, A._43);
			it[3].set(A._14, A._24, A._34, A._44);
			bChanged = true;
		}
		break;
	default:
#ifdef DEBUG
		Debug.fatal		(DEBUG_INFO,"Invalid constant run-time-type for '%s'",*C->name);
#else
		NODEFAULT;
#endif
	}
#if defined(USE_VK)
	if (bChanged) for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
	if (bChanged) m_bChanged = true;
#endif
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, const Fvector4& A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);

	VERIFY(u32((u32)L.index+lineSize) <= m_uiBufferSize);
	float* it = (float*)Access(L.index);

	size_t count = 4;
	switch (L.cls)
	{
	case RC_1x2: count = 2; break;
	case RC_1x3: count = 3; break;
	case RC_1x4: count = 4; break;
	default: break;
	}

	if (memcmp(it, &A[0], count*sizeof(float)) != 0) {
		CopyMemory(it, &A[0], count*sizeof(float));
#if defined(USE_VK)
		for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
		m_bChanged = true;
#endif
	}
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, float A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x1 == L.cls);
	float* it = (float*)Access(L.index);
	VERIFY(u32((u32)L.index+sizeof(float)) <= m_uiBufferSize);
	if (*it != A) {
		*it = A;
#if defined(USE_VK)
		for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
		m_bChanged = true;
#endif
	}
}

IC void dx10ConstantBuffer::set(R_constant* C, R_constant_load& L, int A)
{
	VERIFY(RC_int == C->type);
	VERIFY(RC_1x1 == L.cls);
	int* it = (int*)Access(L.index);
	VERIFY(u32((u32)L.index+sizeof(int)) <= m_uiBufferSize);
	if (*it != A) {
		*it = A;
#if defined(USE_VK)
		for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
		m_bChanged = true;
#endif
	}
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A)
{
	VERIFY(RC_float == C->type);
	u32 base;
	Fvector4* it;
	bool bChanged = false;
	
	switch (L.cls)
	{
	case RC_2x4:
		base = (u32)L.index + 2 * lineSize * e;
		it = Access((u16)base);
		VERIFY((base+2*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			bChanged = true;
		}
		break;
	case RC_3x4:
		base = (u32)L.index + 3 * lineSize * e;
		it = Access((u16)base);
		VERIFY((base+3*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42 ||
			it[2].x != A._13 || it[2].y != A._23 || it[2].z != A._33 || it[2].w != A._43) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			it[2].set(A._13, A._23, A._33, A._43);
			bChanged = true;
		}
		break;
	case RC_4x4:
		base = (u32)L.index + 4 * lineSize * e;
		it = Access((u16)base);
		VERIFY((base+4*lineSize) <= m_uiBufferSize);
		if (it[0].x != A._11 || it[0].y != A._21 || it[0].z != A._31 || it[0].w != A._41 ||
			it[1].x != A._12 || it[1].y != A._22 || it[1].z != A._32 || it[1].w != A._42 ||
			it[2].x != A._13 || it[2].y != A._23 || it[2].z != A._33 || it[2].w != A._43 ||
			it[3].x != A._14 || it[3].y != A._24 || it[3].z != A._34 || it[3].w != A._44) {
			it[0].set(A._11, A._21, A._31, A._41);
			it[1].set(A._12, A._22, A._32, A._42);
			it[2].set(A._13, A._23, A._33, A._43);
			it[3].set(A._14, A._24, A._34, A._44);
			bChanged = true;
		}
		break;
	default:
#ifdef DEBUG
		Debug.fatal		(DEBUG_INFO,"Invalid constant run-time-type for '%s'",*C->name);
#else
		NODEFAULT;
#endif
	}
#if defined(USE_VK)
	if (bChanged) for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
	if (bChanged) m_bChanged = true;
#endif
}

IC void dx10ConstantBuffer::seta(R_constant* C, R_constant_load& L, u32 e, const Fvector4& A)
{
	VERIFY(RC_float == C->type);
	VERIFY(RC_1x4 == L.cls || RC_1x3 == L.cls || RC_1x2 == L.cls);

	static const u16 lineSize = 4 * sizeof(float);
	u32 base = (u32)L.index + lineSize * e;
	Fvector4* it = Access((u16)base);
	VERIFY((base+lineSize) <= m_uiBufferSize);
	
	if (it->x != A.x || it->y != A.y || it->z != A.z || it->w != A.w) {
		it->set(A);
#if defined(USE_VK)
		for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
#else
		m_bChanged = true;
#endif
	}
}

IC void* dx10ConstantBuffer::AccessDirect(R_constant_load& L, u32 DataSize)
{
	VERIFY(L.index<(int)m_uiBufferSize);
#if defined(USE_VK)
const u32 workerId = g_vkWorkerId;
BYTE* res = ((BYTE*)m_pBufferData[workerId]) + L.index;
if ((u32)L.index + DataSize <= m_uiBufferSize)
{
for (u32 _i = 0; _i < VK_CB_MAX_WORKERS; ++_i) m_bChanged[_i] = true;
return res;
}
else return 0;
#else
	BYTE* res = ((BYTE*)m_pBufferData) + L.index;

	if ((u32)L.index + DataSize <= m_uiBufferSize)
	{
		// Fallback for direct memory access where we can't easily check what changed.
		// Most things in X-Ray use set() rather than AccessDirect, so this is OK.
		m_bChanged = true;
		return res;
	}
	else return 0;
#endif
}

#endif	//	dx10ConstantBuffer_impl_included
