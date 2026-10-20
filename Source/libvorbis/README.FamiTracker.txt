libvorbis 1.3.7 (https://github.com/xiph/vorbis, tag v1.3.7), for File > Export for games.

Only what FamiTracker needs, as released: include/vorbis/*.h and lib/ without
the tools and tests (barkmel.c, psytune.c, tone.c, misc.c). FamiTracker.vcxproj
and Source/Api/CMakeLists.txt build lib/ with its own folder on the include
path and their warnings off. BSD licensed, see COPYING. The files keep their LF
line endings.
