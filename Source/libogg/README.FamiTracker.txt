libogg 1.3.6 (https://github.com/xiph/ogg, tag v1.3.6), for File > Export for games.

Only what FamiTracker needs, as released: include/ogg/ogg.h and os_types.h,
src/bitwise.c, framing.c and crctable.h. include/ogg/config_types.h is written by
hand (configure or CMake makes it upstream); only systems other than Windows read
it. BSD licensed, see COPYING. The files keep their LF line endings.
