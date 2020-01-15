SRC = src

C_SRCS = $(shell find $(SRC) -name *.c)
CXX_SRCS = $(shell find $(SRC) -name *.cc) $(shell find $(SRC) -name *.cpp)

OBJECTS = $(C_SRCS:.c=.o)
OBJECTS += $(filter-out %.cpp, $(CXX_SRCS:.cc=.o))
OBJECTS += $(filter-out %.cc, $(CXX_SRCS:.cpp=.o))

FLAGS = -fPIC -O3
CFLAGS = $(FLAGS)
CXXFLAGS = $(FLAGS)

LDFLAGS = -shared

all: help

lib%.so: $(OBJECTS)
	@mkdir -p $(@D)
	$(CC) $(LDFLAGS) $(^) $(LDLIBS) -o $(@)

help:
	@echo "Compile the dynamic library."
	@echo "make /path/to/lib<name>.so"
