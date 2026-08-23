#include "stdafx.h"
#pragma hdrstop

#include "r_backend_tree.h"

R_tree::R_tree()
{
	unmap();
}

void R_tree::unmap()
{
	c_m_xform_v = 0;
	c_m_xform = 0;
	c_consts = 0;
	c_wave = 0;
	c_wind = 0;
	c_c_scale = 0;
	c_c_bias = 0;
	c_c_sun = 0;
}

//#if defined(USE_VK) in this file may not be the best solution for the VK multi-thread g-buffer rendering architecture, may need to revisit.
void R_tree::set_m_xform_v(Fmatrix& mat)
{
#if defined(USE_VK)
	if (!c_m_xform_v) c_m_xform_v = RCache.get_c("m_xform_v");
#endif
	if (c_m_xform_v) RCache.set_c(c_m_xform_v, mat);
}

void R_tree::set_m_xform(Fmatrix& mat)
{
#if defined(USE_VK)
	if (!c_m_xform) c_m_xform = RCache.get_c("m_xform");
#endif
	if (c_m_xform) RCache.set_c(c_m_xform, mat);
}

void R_tree::set_consts(float x, float y, float z, float w)
{
#if defined(USE_VK)
	if (!c_consts) c_consts = RCache.get_c("consts");
#endif
	if (c_consts) RCache.set_c(c_consts, x, y, z, w);
}

void R_tree::set_wave(Fvector4& vec)
{
#if defined(USE_VK)
	if (!c_wave) c_wave = RCache.get_c("wave");
#endif
	if (c_wave) RCache.set_c(c_wave, vec);
}

void R_tree::set_wind(Fvector4& vec)
{
#if defined(USE_VK)
	if (!c_wind) c_wind = RCache.get_c("wind");
#endif
	if (c_wind) RCache.set_c(c_wind, vec);
}

void R_tree::set_c_scale(float x, float y, float z, float w)
{
#if defined(USE_VK)
	if (!c_c_scale) c_c_scale = RCache.get_c("c_scale");
#endif
	if (c_c_scale) RCache.set_c(c_c_scale, x, y, z, w);
}

void R_tree::set_c_bias(float x, float y, float z, float w)
{
#if defined(USE_VK)
	if (!c_c_bias) c_c_bias = RCache.get_c("c_bias");
#endif
	if (c_c_bias) RCache.set_c(c_c_bias, x, y, z, w);
}

void R_tree::set_c_sun(float x, float y, float z, float w)
{
#if defined(USE_VK)
	if (!c_c_sun) c_c_sun = RCache.get_c("c_sun");
#endif
	if (c_c_sun) RCache.set_c(c_c_sun, x, y, z, w);
}
