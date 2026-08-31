#ifndef	dx10ConstantBuffer_included
#define	dx10ConstantBuffer_included
#pragma once

struct R_constant;
struct R_constant_load;

class dx10ConstantBuffer : public xr_resource_named
{
public:
	dx10ConstantBuffer(ID3DShaderReflectionConstantBuffer* pTable);
#if defined(USE_VK)
	dx10ConstantBuffer(const char* name, u32 size);
#endif
	~dx10ConstantBuffer();

	bool Similar(dx10ConstantBuffer& _in);
	ID3DBuffer* GetBuffer() { return m_pBuffer; }
	//review: outside a USE_VK wrapper?
	const shared_str& GetBufferName() const { return m_strBufferName; }

	void Flush();

	//	Set copy data into constant buffer
	//	Plain buffer member
	void set(R_constant* C, R_constant_load& L, const Fmatrix& A);
	void set(R_constant* C, R_constant_load& L, const Fvector4& A);
	void set(R_constant* C, R_constant_load& L, float A);
	void set(R_constant* C, R_constant_load& L, int A);
	//	Array buffer member
	void seta(R_constant* C, R_constant_load& L, u32 e, const Fmatrix& A);
	void seta(R_constant* C, R_constant_load& L, u32 e, const Fvector4& A);

	void* AccessDirect(R_constant_load& L, u32 DataSize);

	// VK-specific accessors for ring buffer allocation
#if defined(USE_VK)
	const void* GetRawData() const { return m_pBufferData; }
	u32 GetRawSize() const { return m_uiBufferSize; }
	bool IsDirty() const { return m_bChanged; }
	u32 GetDynamicOffset() const { return m_vkDynamicOffset; }
	void SetDynamicOffset(u32 offset) { m_vkDynamicOffset = offset; }
	u32 GetFlushFrame() const { return m_vkFlushFrame; }
#endif

private:
	Fvector4* Access(u16 offset);

private:
	shared_str m_strBufferName;
	D3D_CBUFFER_TYPE m_eBufferType;

	//	Buffer data description
	u32 m_uiMembersCRC;
	xr_vector<D3D_SHADER_TYPE_DESC> m_MembersList;
	xr_vector<shared_str> m_MembersNames;

	ID3DBuffer* m_pBuffer;
	u32 m_uiBufferSize; //	Cache buffer size for debug validation
	void* m_pBufferData;
	bool m_bChanged;

#if defined(USE_VK)
	u32 m_vkDynamicOffset = 0;  // Ring buffer offset from last AllocateDynamicUniform
	u32 m_vkFlushFrame = 0xFFFFFFFF; // Frame stamp of the last flush
#endif

	static const u32 lineSize = sizeof(Fvector4);

	//	Never try to copy objects of this class due to the pointer and autoptr members
	dx10ConstantBuffer(const dx10ConstantBuffer&);
	dx10ConstantBuffer& operator=(dx10ConstantBuffer&);
};

typedef resptr_core<dx10ConstantBuffer, resptr_base<dx10ConstantBuffer>> ref_cbuffer;

#endif	//	dx10ConstantBuffer_included
