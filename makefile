OUT := lwebsrv

SRC := \
	src/main.c \
	src/server.c \
	src/template.c \
	src/uri.c \
	src/resource.c \
	src/mime.c \
	src/markdown.c \
	src/http_client.c

LT_PATH := lt
LT_ENV :=

# -----== COMPILER
CC := cc
CC_WARN := -Wall -Werror -Wno-strict-aliasing -Wno-error=unused-variable -Wno-unused-function -Wno-pedantic -Wno-unused-label -Wno-unused-but-set-variable
CC_FLAGS := -I$(LT_PATH)/include/ -std=gnu2x -fmax-errors=3 $(CC_WARN) -mavx2 -masm=intel

ifdef DEBUG
	CC_FLAGS += -fsanitize=undefined -fsanitize=address -fno-omit-frame-pointer -O0 -g -DLT_DEBUG=1
else
	CC_FLAGS += -O2
endif

ifdef SSL
	LT_ENV += LT_SSL=1
	CC_FLAGS += -DSSL=1
endif

# -----== LINKER
LNK := cc
LNK_LIBS :=
LNK_FLAGS :=

ifdef DEBUG
	LNK_LIBS += -lasan -lubsan
	LNK_FLAGS += -g -rdynamic
endif

ifdef SSL
	LNK_LIBS += -lssl -lcrypto
endif

LNK_LIBS += -lpthread -ldl -lm

# -----== TARGETS
ifdef DEBUG
	BIN_PATH := bin/debug
	LT_ENV += DEBUG=1
else
	BIN_PATH := bin/release
endif

OUT_PATH := $(BIN_PATH)/$(OUT)

LT_LIB := $(LT_PATH)/$(BIN_PATH)/lt.a

OBJS := $(patsubst %.c,$(BIN_PATH)/%.o,$(SRC))
DEPS := $(patsubst %.o,%.deps,$(OBJS))

all: $(OUT_PATH)

install: all
	cp $(OUT_PATH) /usr/local/bin/

run: all
	$(OUT_PATH) $(args)

clean:
	-rm -r bin

lt:
	$(LT_ENV) make -C $(LT_PATH)

$(LT_LIB): lt

$(OUT_PATH): $(OBJS) lt
	$(LNK) $(OBJS) $(LT_LIB) $(LNK_LIBS) $(LNK_FLAGS) -o $(OUT_PATH)

$(BIN_PATH)/%.o: %.c makefile
	@-mkdir -p $(BIN_PATH)/$(dir $<)
	$(CC) $(CC_FLAGS) -MD -MT $@ -MF $(patsubst %.o,%.deps,$@) -c $< -o $@

-include $(DEPS)

.PHONY: all install run clean lt
