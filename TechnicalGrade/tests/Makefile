CXXFLAGS = -std=c++14 -O2 -Wall -Wextra

SHARED_HEADERS = ../ColorMath.h ../ColorMathBody.h ../ColorSpaces.h ../Saturation.h ../KernelParams.h ../WhiteBalance.h

.PHONY: run clean metal bundle
run: test_pipeline compile_metal parity_metal load_plugin
	./test_pipeline
	./compile_metal
	./parity_metal
	./load_plugin

test_pipeline: test_pipeline.cpp ../WhiteBalance.cpp ../ColorSpaces.cpp ../Saturation.cpp $(SHARED_HEADERS)
	$(CXX) $(CXXFLAGS) test_pipeline.cpp ../WhiteBalance.cpp ../ColorSpaces.cpp ../Saturation.cpp -o $@

# Compiling the kernel is what proves the shared body is valid MSL as well as
# valid C++. It goes through the Metal runtime rather than the offline metal
# tool so it works on a machine without the Xcode Metal toolchain installed.
compile_metal: compile_metal.mm ../MetalSource.h ../ColorMathSource.h
	$(CXX) $(CXXFLAGS) -Wno-unused-const-variable $< -o $@ -framework Metal -framework Foundation

# Links the plugin's own MetalKernel.mm so the parity test drives the identical
# dispatch Resolve does, buffer-handle-as-float-pointer convention included.
parity_metal: parity_metal.mm ../MetalKernel.mm ../WhiteBalance.cpp ../ColorSpaces.cpp ../Saturation.cpp $(SHARED_HEADERS) ../MetalSource.h ../ColorMathSource.h
	$(CXX) $(CXXFLAGS) -Wno-unused-const-variable parity_metal.mm ../MetalKernel.mm ../WhiteBalance.cpp ../ColorSpaces.cpp ../Saturation.cpp \
		-o $@ -framework Metal -framework Foundation

# Needs the bundle on disk, so build the plugin first.
load_plugin: load_plugin.cpp bundle
	$(CXX) $(CXXFLAGS) -I../../OpenFX-1.4/include $< -o $@

bundle:
	@$(MAKE) -C .. --no-print-directory

# Handy when debugging a shader error: writes out exactly what gets compiled.
kernel.metal: dump_metal_source
	./dump_metal_source > $@

dump_metal_source: dump_metal_source.cpp ../MetalSource.h ../ColorMathSource.h
	$(CXX) $(CXXFLAGS) -Wno-unused-const-variable $< -o $@

# Must depend on the body, not merely on the file's absence, or an edit to the
# maths leaves the GPU compiling yesterday's source while the CPU compiles today's.
../ColorMathSource.h: ../ColorMathBody.h
	$(MAKE) -C .. ColorMathSource.h

clean:
	rm -f test_pipeline dump_metal_source compile_metal parity_metal load_plugin kernel.metal kernel.metallib kernel.air
