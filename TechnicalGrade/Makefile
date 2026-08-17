UNAME_SYSTEM := $(shell uname -s)

PLUGIN_NAME = TechnicalGrade
BUNDLE = $(PLUGIN_NAME).ofx.bundle

CXXFLAGS = -std=c++14 -O3 -fvisibility=hidden -I../OpenFX-1.4/include -I../Support/include

ifneq ($(UNAME_SYSTEM), Darwin)
$(error This Makefile targets macOS only. The Metal render path is Apple-specific.)
endif

ARCH_FLAGS = -arch arm64 -arch x86_64

# Without this the build inherits whatever SDK the machine happens to have and
# refuses to load on anything older, silently: the host just does not list the
# plugin. 11.0 is the floor for arm64 and covers every Resolve that supports
# Apple Silicon, so one number serves both slices.
DEPLOYMENT_TARGET = 11.0
ARCH_FLAGS += -mmacosx-version-min=$(DEPLOYMENT_TARGET)

CXXFLAGS += $(ARCH_FLAGS)
LDFLAGS = -bundle -fvisibility=hidden -framework Metal -framework AppKit
LDFLAGS += $(ARCH_FLAGS)
BUNDLE_DIR = $(BUNDLE)/Contents/MacOS/

SUPPORT_OBJS = ofxsCore.o ofxsImageEffect.o ofxsInteract.o ofxsLog.o ofxsMultiThread.o \
               ofxsParams.o ofxsProperty.o ofxsPropertyValidation.o

PLUGIN_OBJS = TechnicalGradePlugin.o WhiteBalance.o ColorSpaces.o MetalKernel.o

$(PLUGIN_NAME).ofx: $(PLUGIN_OBJS) $(SUPPORT_OBJS)
	$(CXX) $^ -o $@ $(LDFLAGS)
	mkdir -p $(BUNDLE_DIR)
	cp $(PLUGIN_NAME).ofx $(BUNDLE_DIR)
	cp Info.plist $(BUNDLE)/Contents/

TechnicalGradePlugin.o: TechnicalGradePlugin.cpp TechnicalGradePlugin.h ColorMath.h ColorSpaces.h KernelParams.h WhiteBalance.h
	$(CXX) -c $< $(CXXFLAGS)

WhiteBalance.o: WhiteBalance.cpp WhiteBalance.h
	$(CXX) -c $< $(CXXFLAGS)

ColorSpaces.o: ColorSpaces.cpp ColorSpaces.h ColorMath.h KernelParams.h
	$(CXX) -c $< $(CXXFLAGS)

MetalKernel.o: MetalKernel.mm KernelParams.h ColorMathSource.h
	$(CXX) -c $< $(CXXFLAGS)

# The Metal kernel compiles the very same text the CPU path does. Wrapping the
# shared body in a raw string literal here means there is only ever one copy of
# the colour maths to keep correct.
ColorMathSource.h: ColorMathBody.h
	@printf '// Generated from ColorMathBody.h by the Makefile. Do not edit.\nR"MSL(\n' > $@
	@cat $< >> $@
	@printf '\n)MSL"\n' >> $@

%.o: ../Support/Library/%.cpp
	$(CXX) -c $< $(CXXFLAGS)

.PHONY: clean install test
clean:
	rm -f *.o *.ofx ColorMathSource.h
	rm -fr $(BUNDLE)
	$(MAKE) -C tests clean

install: $(PLUGIN_NAME).ofx
	mkdir -p /Library/OFX/Plugins
	rm -fr /Library/OFX/Plugins/$(BUNDLE)
	cp -fr $(BUNDLE) /Library/OFX/Plugins

test:
	$(MAKE) -C tests run
