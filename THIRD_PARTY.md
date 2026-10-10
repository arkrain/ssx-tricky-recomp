# Third-party notices

Project-authored code and patches are distributed under GPL-2.0-or-later.
The full GPL version 2 text is in LICENSE; later versions may be used under
that grant. Existing upstream copyright and license notices remain applicable.

Bootstrap obtains these pinned components:

| Component | Revision | License |
| --- | --- | --- |
| [xboxrecomp](https://github.com/sp00nznet/xboxrecomp) | 6f55eaa29d369860c950f442649e23ca86a3c275 | MIT; notice in LICENSES/xboxrecomp-MIT.txt |
| [nv2a_vsh_cpu](https://github.com/abaire/nv2a_vsh_cpu) | 1115255708c10c4841b65dcd2223262e7a316598 | Unlicense; notice in LICENSES/nv2a_vsh_cpu-Unlicense.txt |
| [extract-xiso](https://github.com/XboxDev/extract-xiso) | 3f5b62cfe68f000b0e3c8a30104973f3a297948e | See LICENSES/extract-xiso.txt |
| [xemu DSP interpreter](https://github.com/xemu-project/xemu) | f9b14039e5bb56ae2d8f028e31e7cc19f13f7e12 | GPL-2.0-or-later core; LGPL-2.0-or-later DMA files |

DSP inputs are fetched individually and checked against tools/dsp-research/
source-hashes.json. Their original per-file notices are preserved. No pinned
upstream checkout or game-derived executable is included in the public source.
Build-time/system dependencies such as SDL2, FFmpeg, OpenSSL, libepoxy and
Capstone retain their respective upstream licenses.

The project's license does not grant rights to original SSX Tricky game assets
or original game code. These must be supplied locally and are never checked in.
