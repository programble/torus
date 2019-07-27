CFLAGS += -DHAVE_KCGI -I/usr/local/include
LDFLAGS += -L/usr/local/lib
LDLIBS := -lkcgi ${LDLIBS}
