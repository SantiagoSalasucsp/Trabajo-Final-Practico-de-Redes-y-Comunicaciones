# Compilador y flags
CXX       := g++
CXXFLAGS  := -std=c++17 -Wall -O2
LDFLAGS   := -pthread            # hilo + sockets

# Fuentes / objetos / destino
SRCS   := server.cpp tcp.cpp
OBJS   := $(SRCS:.cpp=.o)
TARGET := server

# Regla por defecto
all: $(TARGET)

# Enlazado
$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

# Compilación individual de .cpp -> .o
%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Limpieza
clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean