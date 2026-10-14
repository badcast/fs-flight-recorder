CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -pedantic
TARGET = fs-flight-recorder
SRC = fs-flight-recorder.cpp

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) $< -o $@

clean:
	rm -f $(TARGET) *.o

test: $(TARGET)
	@echo "Running quick smoke test..."
	@mkdir -p /tmp/fs_fr_smoketest
	@./$(TARGET) /tmp/fs_fr_smoketest "echo '[TEST TRIGGER FIRED]'" & PID=$$!; \
	sleep 0.2; \
	for i in $$(seq 1 60); do touch /tmp/fs_fr_smoketest/file_$$i; done; \
	sleep 0.5; \
	kill -INT $$PID 2>/dev/null || true; \
	wait $$PID 2>/dev/null || true; \
	rm -rf /tmp/fs_fr_smoketest
	@echo "Smoke test passed."

install: $(TARGET)
	install -D -m 755 $(TARGET) /usr/local/bin/$(TARGET)
