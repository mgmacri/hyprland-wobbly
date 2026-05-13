PLUGIN_NAME = hyprland-wobbly

SOURCES = src/main.cpp src/WobblyModel.cpp
OBJS    = $(SOURCES:.cpp=.o)

PKG_CFLAGS  = $(shell pkg-config --cflags hyprland pixman-1 libdrm pangocairo)
PKG_LIBS    = $(shell pkg-config --libs   pixman-1 libdrm pangocairo)

CXX      ?= g++
CXXFLAGS += -std=c++26 -fPIC -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-variable
CXXFLAGS += -DWLR_USE_UNSTABLE
CXXFLAGS += $(PKG_CFLAGS)

LDFLAGS  += -shared

all: $(PLUGIN_NAME).so

$(PLUGIN_NAME).so: $(OBJS)
	$(CXX) $(LDFLAGS) -o $@ $(OBJS) $(PKG_LIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(PLUGIN_NAME).so

.PHONY: all clean
