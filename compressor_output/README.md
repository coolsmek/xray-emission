### Output directory from running "combine_and_compress_gamedata_to_db.bat"

- combine_and_compress_gamedata_to_db.bat copies the root 'gamedata' dir into a temp dir inside .compressor'. Then is will copy the 'gamedata_vk' into the temp dir, verifying and requesting user input for any merge conflicts.
- Once a successful merge is complete it will package/compress the gamedata into a .db0 file named "00_modded_exes_gamedata.db0" and move it to the 'compressor_output' dir.
