stb_truetype.h v1.26 by Sean Barrett (https://github.com/nothings/stb, commit
5c205738c191bcb0abc65c4febfa9bd25ff35234). Public domain or MIT, at your choice (see the end of
the file). gdi32.cpp compiles its implementation (STB_TRUETYPE_IMPLEMENTATION, STBTT_STATIC).

Local changes, each marked "Freelancer Decomp" in the source:

- The CFF buffer ends where the `CFF ` table does (stbtt__find_table_length), not 512 MB on.
- A Unicode-platform cmap record of format 14 (variation sequences) does not replace the base map.
- A nonzero format 4 glyph-array entry takes the segment's idDelta (modulo 65536), as the spec says.
- A failed allocation of CFF vertices or of the rasterizer's scanline draws nothing, and a failed
  second CFF pass frees its vertices.

gdi32.cpp checks a font file's structure (table directory, cmap, name, hmtx, loca and each
TrueType outline) before stb_truetype reads it, since stb_truetype does not bound its reads.
