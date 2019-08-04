name=libens.so

obj=alist.o buffer.o ens.o queue.o

cc=gcc
#cflags=-D_GNU_SOURCE -fPIC -Wall -g
cflags=`curl-config --cflags` -fPIC -Wall -D_GNU_SOURCE -g
ldflags=`curl-config --libs` -lpthread -shared

all: $(name)

$(name): $(obj)
	$(cc) -o $@ $^ $(ldflags)

%.o: %.c
	$(cc) -o $@ -c $< $(cflags)

install:
	cp -f ../api/ens.h /usr/local/include
	cp -f libens.so /usr/local/lib

clean:
	rm -f $(obj) $(name)
