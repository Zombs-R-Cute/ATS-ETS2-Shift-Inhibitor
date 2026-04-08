SDK_HEADERS=\
	include/*.h \
	include/common/*.h \
	include/amtrucks/*.h \
	include/eurotrucks2/*.h

SDK_INCLUDES=\
	-Iinclude \
	-Iinclude/common/ \
	-Iinclude/amtrucks/ \
	-Iinclude/eurotrucks2

UNAME:= $(shell uname -s)

ifeq ($(UNAME),Darwin)
LIB_NAME_OPTION=-install_name
else
LIB_NAME_OPTION=-soname
endif

telemetry.so:  *.cpp $(SDK_HEADERS)
	g++ -o $@ -fPIC -Wall --shared -Wl,$(LIB_NAME_OPTION),$@ $(SDK_INCLUDES) *.cpp -L/usr/lib -lserial

.PHONY: clean
clean:
	@rm -f -- *.so
