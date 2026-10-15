# Wine's MS ADPCM codec, with Windows' names

`msadp32.c` and `msacmdrv.h` come from Wine (`dlls/msadp32.acm/msadp32.c`,
`include/msacmdrv.h`, master branch as of 2026-10-15) and stay under the
GNU LGPL 2.1 or later (`COPYING.LIB`).

The one change: the driver reports the names Windows uses, "Microsoft ADPCM" /
"Microsoft ADPCM CODEC", instead of Wine's "MS-ADPCM" / "Wine MS ADPCM converter".
Games that search the installed codecs by name, such as Hasbro's Classic Board
Games, otherwise fail with "Could not initialize sound". Wine's IMA ADPCM driver
already uses its Windows name.

`wine/debug.h` is FRAG's stand-in that drops Wine's logging, so the file builds
with MinGW. `msadp32.def` exports `DriverProc` as Wine's spec file does.

FRAG copies the built `msadp32.acm` into each prefix and loads it with a
`msadp32.acm=native,builtin` DLL override. The same change could go to Wine
upstream, after which this copy can be dropped.
