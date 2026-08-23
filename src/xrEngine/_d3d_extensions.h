#ifndef _D3D_EXT_internal
#define _D3D_EXT_internal

#ifndef NO_XR_LIGHT
struct Flight
{
public:
	u32 type; /* Type of light source */
	Fcolor diffuse; /* Diffuse color of light */
	Fcolor specular; /* Specular color of light */
	Fcolor ambient; /* Ambient color of light */
	Fvector position; /* Position in world space */
	Fvector direction; /* Direction in world space */
	float range; /* Cutoff range */
	float falloff; /* Falloff */
	float attenuation0; /* Constant attenuation */
	float attenuation1; /* Linear attenuation */
	float attenuation2; /* Quadratic attenuation */
	float theta; /* Inner angle of spotlight cone */
	float phi; /* Outer angle of spotlight cone */

	IC void set(u32 ltType, float x, float y, float z)
	{
		ZeroMemory(this, sizeof(Flight));
		type = ltType;
		diffuse.set(1.0f, 1.0f, 1.0f, 1.0f);
		specular.set(diffuse);
		position.set(x, y, z);
		direction.set(x, y, z);
		direction.normalize_safe();
		range = _sqrt(flt_max);
	}

	IC void mul(float brightness)
	{
		diffuse.mul_rgb(brightness);
		ambient.mul_rgb(brightness);
		specular.mul_rgb(brightness);
	}
};

/*
#if sizeof(Flight)!=sizeof(D3DLIGHT9)
#error Different structure size
#endif
*/

#endif

#ifndef NO_XR_MATERIAL
struct Fmaterial
{
public:
	Fcolor diffuse; /* Diffuse color RGBA */
	Fcolor ambient; /* Ambient color RGB */
	Fcolor specular; /* Specular 'shininess' */
	Fcolor emissive; /* Emissive color RGB */
	float power; /* Sharpness if specular highlight */

	IC void set(float r, float g, float b)
	{
		ZeroMemory(this, sizeof(Fmaterial));
		diffuse.r = ambient.r = r;
		diffuse.g = ambient.g = g;
		diffuse.b = ambient.b = b;
		diffuse.a = ambient.a = 1.0f;
		power = 0;
	}

	IC void set(float r, float g, float b, float a)
	{
		ZeroMemory(this, sizeof(Fmaterial));
		diffuse.r = ambient.r = r;
		diffuse.g = ambient.g = g;
		diffuse.b = ambient.b = b;
		diffuse.a = ambient.a = a;
		power = 0;
	}

	IC void set(Fcolor& c)
	{
		ZeroMemory(this, sizeof(Fmaterial));
		diffuse.r = ambient.r = c.r;
		diffuse.g = ambient.g = c.g;
		diffuse.b = ambient.b = c.b;
		diffuse.a = ambient.a = c.a;
		power = 0;
	}
};

/*
#if sizeof(Fmaterial)!=sizeof(D3DMATERIAL9)
#error Different structure size
#endif
*/

#endif

#ifndef NO_XR_VDECLARATOR
#if defined(USE_DX10) || defined(USE_DX11) || defined(USE_VK)
struct VDeclarator : public svector<D3DVERTEXELEMENT9, MAXD3DDECLLENGTH + 1>
{
	void set(u32 FVF)
	{
		// D3DXDeclaratorFromFVF not available in DX10/11/VK; handled by specific conversion layers elsewhere if needed
		resize(1); // minimal valid state
	}

	void set(D3DVERTEXELEMENT9* dcl)
	{
		// D3DXGetDeclLength not available; count until D3DDECL_END()
		u32 declLength = 0;
		while (dcl[declLength].Stream != 0xFF) { declLength++; }
		resize(declLength + 1);
		CopyMemory(begin(), dcl, size() * sizeof(D3DVERTEXELEMENT9));
	}

	void set(const VDeclarator& d)
	{
		*this = d;
	}

	u32 vertex()
	{
		// Compute stride as max(Offset + elementSize) across all stream-0 elements.
		// Mirrors D3DXGetDeclVertexSize semantics used by the DX9 build path.
		static const u32 kTypeSize[] = {
			4,8,12,16,  // FLOAT1..FLOAT4
			4,4,4,8,    // D3DCOLOR, UBYTE4, SHORT2, SHORT4
			4,4,8,4,8,  // UBYTE4N, SHORT2N, SHORT4N, USHORT2N, USHORT4N
			4,4,4,8     // UDEC3, DEC3N, FLOAT16_2, FLOAT16_4
		};
		u32 stride = 0;
		for (u32 i = 0; i < size() && begin()[i].Stream != 0xFF; ++i) {
			if (begin()[i].Stream != 0) continue;
			u8 t = begin()[i].Type;
			u32 eSize = (t < sizeof(kTypeSize)/sizeof(kTypeSize[0])) ? kTypeSize[t] : 0;
			u32 end = begin()[i].Offset + eSize;
			if (end > stride) stride = end;
		}
		return stride;
	}

	BOOL equal(VDeclarator& d)
	{
		if (size() != d.size()) return false;
		else return 0 == memcmp(begin(), d.begin(), size() * sizeof(D3DVERTEXELEMENT9));
	}
};
#else
struct VDeclarator : public svector<D3DVERTEXELEMENT9, MAXD3DDECLLENGTH + 1>
{
	void set(u32 FVF)
	{
		D3DXDeclaratorFromFVF(FVF, begin());
		resize(D3DXGetDeclLength(begin()) + 1);
	}

	void set(D3DVERTEXELEMENT9* dcl)
	{
		resize(D3DXGetDeclLength(dcl) + 1);
		CopyMemory(begin(), dcl, size()*sizeof(D3DVERTEXELEMENT9));
	}

	void set(const VDeclarator& d)
	{
		*this = d;
	}

	u32 vertex() { return D3DXGetDeclVertexSize(begin(), 0); }

	BOOL equal(VDeclarator& d)
	{
		if (size() != d.size()) return false;
		else return 0 == memcmp(begin(), d.begin(), size() * sizeof(D3DVERTEXELEMENT9));
	}
};
#endif
#endif

#endif
