# Builds every probe for x64 and x86 with llvm-mingw. Run through
# build/probes/build.sh, which provides the toolchain in Docker.
#   make OUT=<dir> BUILD=<objdir>

OUT ?= ../out/probes
BUILD ?= $(OUT)/obj
ARCHES := x64 x86
PROBES := d3d9 d3d11 d3d12 opengl vulkan

triple_x64 := x86_64-w64-mingw32
triple_x86 := i686-w64-mingw32

CFLAGS := -O2 -Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers \
          -municode -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 \
          -I/opt/vulkan-headers/include -I$(BUILD)/gen
CXXFLAGS := $(CFLAGS) -std=c++17 -fno-exceptions -fno-rtti
# Static C++ runtime, GUI subsystem, deterministic output: the only imports
# left are Windows system DLLs (build/probes/check-imports.sh proves it).
LDFLAGS := -municode -mwindows -static -s -Wl,--no-insert-timestamp

libs_d3d9 := -ld3d9
libs_d3d11 := -ld3d11 -ldxgi
libs_d3d12 := -ld3d12 -ldxgi
libs_opengl := -lopengl32 -lgdi32
libs_vulkan :=

src_d3d9 := d3d9/d3d9_probe.cpp
src_d3d11 := d3d11/d3d11_probe.cpp
src_d3d12 := d3d12/d3d12_probe.cpp
src_opengl := opengl/opengl_probe.c
src_vulkan := vulkan/vulkan_probe.c

HEADERS := common/probe.h common/font.h common/dxgi_util.h

all: $(foreach a,$(ARCHES),$(foreach p,$(PROBES),$(OUT)/$(a)/$(p)_probe.exe))

$(BUILD)/gen/vulkan_shaders.h: vulkan/probe.vert vulkan/probe.frag
	@mkdir -p $(dir $@)
	glslangValidator -V --vn probe_vert_spv -o $(BUILD)/gen/vert.h vulkan/probe.vert
	glslangValidator -V --vn probe_frag_spv -o $(BUILD)/gen/frag.h vulkan/probe.frag
	cat $(BUILD)/gen/vert.h $(BUILD)/gen/frag.h > $@

define common_rules
$(BUILD)/$(1)/probe.o: common/probe.c $(HEADERS)
	@mkdir -p $$(dir $$@)
	$(triple_$(1))-clang $(CFLAGS) -c -o $$@ $$<
endef

compile = $(if $(filter %.cpp,$(1)),$(triple_$(2))-clang++ $(CXXFLAGS),$(triple_$(2))-clang $(CFLAGS))

define probe_rules
$(BUILD)/$(1)/$(2).o: $(src_$(2)) $(HEADERS) $(BUILD)/gen/vulkan_shaders.h
	@mkdir -p $$(dir $$@)
	$(call compile,$(src_$(2)),$(1)) -c -o $$@ $$<

$(OUT)/$(1)/$(2)_probe.exe: $(BUILD)/$(1)/$(2).o $(BUILD)/$(1)/probe.o
	@mkdir -p $$(dir $$@)
	$(triple_$(1))-clang++ $(LDFLAGS) -o $$@ $$^ $(libs_$(2))
endef

$(foreach a,$(ARCHES),$(eval $(call common_rules,$(a))))
$(foreach a,$(ARCHES),$(foreach p,$(PROBES),$(eval $(call probe_rules,$(a),$(p)))))

clean:
	rm -rf $(BUILD) $(foreach a,$(ARCHES),$(OUT)/$(a))

.PHONY: all clean
