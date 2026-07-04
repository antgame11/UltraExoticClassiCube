# ----------------------------
# Makefile Options for ClassiCube TI-84 Plus CE Port
# ----------------------------

NAME ?= CLASSCUB
ICON ?= icon.png
DESCRIPTION ?= "ClassiCube Port"
COMPRESSED ?= YES
ARCHIVED ?= YES

# Point the build system to parent directory containing ClassiCube source files
SRCDIR = ..
OBJDIR = obj

# List of all C files under consideration
ALL_C := $(wildcard $(SRCDIR)/*.c) $(wildcard ./*.c)

# Filter out FreeType files
ALL_C := $(filter-out $(SRCDIR)/_%, $(ALL_C))

# Filter out unused platform, window, graphics, and audio drivers
ALL_C := $(filter-out $(SRCDIR)/Platform_%, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Window_%, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Audio_%, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/LBackend%, $(ALL_C))

# Filter out specific unused graphics backends
ALL_C := $(filter-out $(SRCDIR)/Graphics_D3D%, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Graphics_GL%, $(ALL_C))

# Filter out specific unused files (Launcher, Networking, Audio decoders)
ALL_C := $(filter-out $(SRCDIR)/Launcher.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/LScreens.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/LWeb.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/LWidgets.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Vorbis.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/SSL.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Certs.c, $(ALL_C))
ALL_C := $(filter-out $(SRCDIR)/Http_Worker.c, $(ALL_C))

# Re-include required stubs that were filtered out by broad patterns
REQUIRED_DRIVERS := $(SRCDIR)/Audio_Null.c $(SRCDIR)/Http_Worker.c

# Override the SDK's recursive wildcard function to use our filtered list
override rwildcard = $(filter $(subst *,%,$2), $(ALL_C) $(REQUIRED_DRIVERS))

# Compilation flags for the eZ80 clang/LLVM compiler
# -DPLAT_TI84: Activates target block in Core.h
# -DCC_BUILD_LOWMEM: Limits buffer allocation sizes
# -DCC_BUILD_FIXEDPOINT: Forces fixed-point math pipeline
CFLAGS = -Wall -Wextra -Oz -DPLAT_TI84 -DCC_BUILD_LOWMEM -DCC_BUILD_FIXEDPOINT
CXXFLAGS = -Wall -Wextra -Oz -DPLAT_TI84 -DCC_BUILD_LOWMEM -DCC_BUILD_FIXEDPOINT

# ----------------------------

include $(shell cedev-config --makefile)
