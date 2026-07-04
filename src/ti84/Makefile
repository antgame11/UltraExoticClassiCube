# ----------------------------
# Makefile Options for ClassiCube TI-84 Plus CE Port
# ----------------------------

NAME ?= CLASSCUB
ICON ?= icon.png
DESCRIPTION ?= "ClassiCube Port"
COMPRESSED ?= YES
ARCHIVED ?= NO

# Point the build system to parent directory containing ClassiCube source files
SRCDIR = ..
OBJDIR = obj

# Compilation flags for the eZ80 clang/LLVM compiler
# -DPLAT_TI84: Activates target block in Core.h
# -DCC_BUILD_LOWMEM: Limits buffer allocation sizes
# -DCC_BUILD_FIXEDPOINT: Forces fixed-point math pipeline
CFLAGS = -Wall -Wextra -Oz -DPLAT_TI84 -DCC_BUILD_LOWMEM -DCC_BUILD_FIXEDPOINT
CXXFLAGS = -Wall -Wextra -Oz -DPLAT_TI84 -DCC_BUILD_LOWMEM -DCC_BUILD_FIXEDPOINT

# ----------------------------

include $(shell cedev-config --makefile)
