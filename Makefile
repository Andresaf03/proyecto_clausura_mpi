# Compiler configuration (puede sobrescribirse al invocar make MPI_CXX=...).
MPI_CXX ?= mpicxx
CXXFLAGS ?= -O2 -std=c++17 -Wall -Wextra -pedantic
INCLUDES = -Iinclude

# Rutas principales.
BUILD_DIR = build
TARGET = $(BUILD_DIR)/bow_app
SOURCES = src/main.cpp src/serial.cpp src/paralelo.cpp

.PHONY: all clean dirs

all: dirs $(TARGET)

dirs:
	@mkdir -p $(BUILD_DIR)

$(TARGET): $(SOURCES)
	$(MPI_CXX) $(CXXFLAGS) $(INCLUDES) $(SOURCES) -o $(TARGET)

clean:
	rm -rf $(BUILD_DIR)
