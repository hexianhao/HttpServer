BIN_DIR:=bin/
SRC_DIR:=src/
TARGET:=Server

CC		:= gcc
LIBS	:= -lpthread -ltcmalloc
INCLUDE	:= -I./include
CFLAGS	:= -g -Wall -D_GNU_SOURCE -D__USE_XOPEN

SRC:=$(wildcard $(SRC_DIR)*.c)
OBJS_SRC:=$(patsubst $(SRC_DIR)%, $(BIN_DIR)%, $(patsubst %.c, %.o, $(SRC)))

all: $(BIN_DIR)$(TARGET)
.PHONY: all

$(BIN_DIR)$(TARGET) : $(OBJS_SRC)
	@mkdir -p $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $^ $(INCLUDE) $(LIBS)

$(BIN_DIR)%.o: $(SRC_DIR)%.c
	$(CC) -o $@ -c $^ $(CFLAGS) $(CFLAGS) $(INCLUDE) $(LIBS)

.PHONY:clean

clean:
	@rm -f $(BIN_DIR)*
	@rm -f log/*