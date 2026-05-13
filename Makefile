CXX      := g++
CXXFLAGS := -std=c++23 -O3 -march=native \
            -fexceptions -fPIC -shared \
            $(shell pkg-config --cflags hyprland pixman-1 wayland-client libdrm 2>/dev/null || echo "-I/usr/include/hyprland -I/usr/include/pixman-1 -I/usr/include/libdrm") \
            -I/usr/include/hyprland/.. \
            -I/usr/include/hyprland/protocols \
            -I/usr/include/hyprland/src \
            -Wall -Wextra -Wno-error

TARGET   := hyprland-wobbly.so

.PHONY: all clean install

all: $(TARGET)

$(TARGET): main.o WobblyModel.o WobblyTransformer.o
	$(CXX) $(CXXFLAGS) -o $@ $^ -lGLESv2
	@echo "✅ Built $(TARGET)"

main.o: src/main.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

WobblyModel.o: src/WobblyModel.cpp src/WobblyModel.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

WobblyTransformer.o: src/WobblyTransformer.cpp src/WobblyTransformer.hpp src/WobblyModel.hpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f *.o $(TARGET)
	@echo "🧹 Cleaned"

install: $(TARGET)
	mkdir -p ~/.local/share/hypr/plugins/
	cp $(TARGET) ~/.local/share/hypr/plugins/
	@echo "📦 Installed"
