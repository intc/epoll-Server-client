# Define targets:
S_TARGET = epoll_server       # Standard (server)
S_TGTDEB = dbg_$(S_TARGET)    # Debug    (server)
C_TARGET = epoll_client       # Standard (client)
C_TGTDEB = dbg_$(C_TARGET)    # Debug    (client)

# DEFS = -DPERSISTENT_CONN      # Enable this for persistent connection (and run make clean; make all)
LIBS = 

CC = gcc
COMMON_CF = -std=c11 -fomit-frame-pointer -Wall -Wmissing-prototypes\
			-Wstrict-prototypes -Wextra -Wpedantic
CFDBUG = -O0 -ggdb -DDEBUG $(COMMON_CF)
CFLAGS = -O2 -DNDEBUG $(COMMON_CF)

S_SOURCES = epoll_server.c
C_SOURCES = epoll_client.c

.PHONY: clean all debug

%r_std.o: $(S_SOURCES)
	$(CC) $(DEFS) $(CFLAGS) -c -o $@ $<

%r_debug.o: $(S_SOURCES)
	$(CC) $(DEFS) $(CFDBUG)  -c -o $@ $<

%t_std.o: $(C_SOURCES)
	$(CC) $(DEFS) $(CFLAGS) -c -o $@ $<

%t_debug.o: $(C_SOURCES)
	$(CC) $(DEFS) $(CFDBUG)  -c -o $@ $<

all: $(S_TARGET) $(C_TARGET)
	@echo
	@printf "Optimized binary for server: $(S_TARGET)\n"
	@printf "Optimized binary for client: $(C_TARGET)\n"

debug: $(S_TGTDEB) $(C_TGTDEB)
	@echo
	@printf "Debug binary for server: $(S_TGTDEB)\n"
	@printf "Debug binary for client: $(S_TGTDEB)\n"

epoll_server: epoll_server_std.o
	$(CC) $^ $(LIBS) -o $@

dbg_epoll_server: epoll_server_debug.o
	$(CC) $^ $(LIBS) -o $@

epoll_client: epoll_client_std.o
	$(CC) $^ $(LIBS) -o $@

dbg_epoll_client: epoll_client_debug.o
	$(CC) $^ $(LIBS) -o $@

clean:
	$(RM) -rf \
		$(S_TARGET) $(S_TGTDEB)\
		$(C_TARGET) $(C_TGTDEB)\
		*.o\
