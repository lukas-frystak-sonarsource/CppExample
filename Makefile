CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra
TARGET   = cppexample
SRCDIR   = src
SRCS     = $(SRCDIR)/main.cpp
OBJS     = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
