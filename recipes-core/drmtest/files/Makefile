
Q := @

CFLAGS += -I. -I${STAGING_INCDIR}/xrt
LDFLAGS += -lrt -lpthread -lstdc++ -lxrt_core -lxilinxopencl -ljsoncpp -laccelize_drm

drmtest: drmtest.cpp
	@echo "  $(CXX)      $<"
	$(Q)$(CXX) $(CFLAGS) -std=c++11 $^ -o $@ $(LDFLAGS)

all: drmtest

clean:
	@rm -f drmtest

.PHONY: clean
