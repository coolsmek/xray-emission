#include "stdafx.h"
#pragma hdrstop

#include "r_backend_hemi.h"

R_hemi::R_hemi()
{
	unmap();
}

void R_hemi::unmap()
{
	c_pos_faces = 0;
	c_neg_faces = 0;
	c_material = 0;
	c_hotness = 0; //--DSR-- HeatVision
	c_glowing = 0; //--DSR-- SilencerOverheat
}

void R_hemi::set_pos_faces(float posx, float posy, float posz)
{
	if (c_pos_faces) RCache.set_c(c_pos_faces, posx, posy, posz, 0);
}

void R_hemi::set_neg_faces(float negx, float negy, float negz)
{
	if (c_neg_faces) RCache.set_c(c_neg_faces, negx, negy, negz, 0);
}

void R_hemi::set_material(float x, float y, float z, float w)
{
	if (c_material) {
		RCache.set_c(c_material, x, y, z, w);
	} else {
#ifdef DEBUG
		static xr_vector<shared_str> logged_shaders;
		shared_str current_ps = RCache.ps_name ? RCache.ps_name : "unknown_ps";
		bool bFound = false;
		for (u32 i = 0; i < logged_shaders.size(); ++i) {
			if (logged_shaders[i] == current_ps) { bFound = true; break; }
		}
		if (!bFound) {
			logged_shaders.push_back(current_ps);
			Msg("![hemi] set_material SKIPPED: c_material==null in shader '%s' (mtl=%.3f)", current_ps.c_str(), y);
		}
#else
		static bool bLogged = false;
		if (!bLogged) {
			Msg("![hemi] set_material SKIPPED: c_material==null (mtl=%.3f)", y);
			bLogged = true;
		}
#endif
	}
}

//--DSR-- HeatVision_start
void R_hemi::set_hotness(float x, float y, float z, float w)
{
	if (c_hotness) RCache.set_c(c_hotness, x, y, z, w);
}
//--DSR-- HeatVision_end

//--DSR-- SilencerOverheat_start
void R_hemi::set_glowing(float x, float y, float z, float w)
{
	if (c_glowing) RCache.set_c(c_glowing, x, y, z, w);
}
//--DSR-- SilencerOverheat_end