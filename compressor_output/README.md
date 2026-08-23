### Output directory from running "combine_and_compress_gamedata_to_db.bat"
Contains "modded exe's" gamedata and vulkan specific gamedata into one .db0 package.
- 'combine_and_compress_gamedata_to_db.bat' copies the root 'gamedata' dir into a temp dir inside 'compressor'. It will then merge the 'gamedata_vk' into the same temp dir, verifying and requesting user input for any merge conflicts.
- Once a successful merge is complete it will package/compress the gamedata into a .db0 file named "000_modded_exes_gamedata.db0" and move it to the 'compressor_output' dir.
- Use "000_modded_exes_gamedata.db0" in the STLAKER ANOMALY installation\db\mods folder to load at runtime.
