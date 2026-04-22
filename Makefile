CXX      = g++
CXXFLAGS = -std=c++17 -Wall -Wextra -I include
LDFLAGS  = -lssl -lcrypto
TARGET   = cppexample
SRCDIR   = src
SRCS     = $(SRCDIR)/main.cpp
OBJS     = $(SRCS:.cpp=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CXX) $(CXXFLAGS) -o $@ $^ $(LDFLAGS)

$(SRCDIR)/%.o: $(SRCDIR)/%.cpp
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
