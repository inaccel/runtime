RUNTIMES = intel-fpga xilinx-fpga

all: $(RUNTIMES)

define TEMPLATE
$(1): | eval-$(1) src/$(1)/a.out

.INTERMEDIATE: $(2:%=src/$(1)/%.o)
src/$(1)/a.out: $(2:%=src/$(1)/%.o)
endef

eval-%:
	$(eval CFLAGS = -I include $($*_CFLAGS))
	$(eval CXXFLAGS = -I include $($*_CXXFLAGS))
	$(eval CPPFLAGS = $($*_CPPFLAGS))
	$(eval LDFLAGS = $($*_LDFLAGS))

$(foreach RUNTIME,$(RUNTIMES),$(eval include src/$(RUNTIME)/.mk))
$(foreach RUNTIME,$(RUNTIMES),$(eval $(call TEMPLATE,$(RUNTIME),$($(RUNTIME)))))

%/a.out:
	$(LINK.o) $^ $(OUTPUT_OPTION)

%.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

%.o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<
