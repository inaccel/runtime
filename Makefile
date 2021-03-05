RUNTIMES = intel-fpga xilinx-fpga

.SECONDEXPANSION:
.SUFFIXES:

CONFIGS = configs
INCLUDE = include
SRC = src

$(RUNTIMES): $(CONFIGS)/$$@/a.out

define TEMPLATE
$(eval include $(SRC)/$(1)/.mk)

# COMPILE.c COMPILE.cpp LINK.o
%/$(1)/a.out: CFLAGS = -I $(INCLUDE) $($(1)_CFLAGS)
%/$(1)/a.out: CXXFLAGS = -I $(INCLUDE) $($(1)_CXXFLAGS)
%/$(1)/a.out: CPPFLAGS = $($(1)_CPPFLAGS)
%/$(1)/a.out: LDFLAGS = $($(1)_LDFLAGS)

%/$(1)/a.out: $(addprefix $(SRC)/$(1)/,$(addsuffix .o,$($(1))))
	$$(LINK.o) $$^ $$(OUTPUT_OPTION)
endef

%.o: %.c
	$(COMPILE.c) $(OUTPUT_OPTION) $<

%.o: %.cpp
	$(COMPILE.cpp) $(OUTPUT_OPTION) $<

$(foreach RUNTIME,$(RUNTIMES),$(eval $(call TEMPLATE,$(RUNTIME))))
