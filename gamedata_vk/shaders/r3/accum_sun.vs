#include "common.h"

//////////////////////////////////////////////////////////////////////////////////////////
uniform float4x4	m_texgen;
#ifdef	USE_SJITTER
uniform float4x4	m_texgen_J;
#endif

//////////////////////////////////////////////////////////////////////////////////////////
#ifdef USE_VK
// VK feeds a screen-space fullscreen quad (FVF::TL): P.xy = screen pixels already in
// the same space combine_1.vs consumes; TEXCOORD0 = [0,1] screen UV.
// We bypass the world-space m_WVP/m_texgen projection (which VK never sets) and emit
// clip-space + tc directly, exactly like the working combine_1.vs.
struct v_accum_vk
{
	float4 P     : POSITIONT;   // xy = screen pos (matches combine_1.vs input space)
	float4 color : COLOR;
	float2 uv    : TEXCOORD0;   // [0,1] screen-space tc
};

v2p_volume main ( v_accum_vk I )
{
	v2p_volume O;
	O.hpos = float4( I.P.x, -I.P.y, I.P.z, 1 );   // same transform as combine_1.vs line 20
	O.tc   = float4( I.uv.x, I.uv.y, 0, 1 );  // PS does tc.xy/tc.w -> (u,v)
#ifdef USE_SJITTER
	O.tcJ  = float4( I.uv.x, I.uv.y, 0, 1 );  // not used by _nominmax variant
#endif
	return O;
}
#else
// Vertex (original DX world-space volume path)
v2p_volume main ( float4 P: POSITION )
{
	v2p_volume	O;
	O.hpos 		= mul( m_WVP, P );
	O.tc 		= mul( m_texgen, P );
#ifdef	USE_SJITTER
	O.tcJ 		= mul( m_texgen_J, P );
#endif
 	return	O;
}
#endif
FXVS;
