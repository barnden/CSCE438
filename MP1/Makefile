all: server client
debug: debug-server debug-client

client: crc.c
	g++ -O3 -std=c++17 -o crc crc.c -lpthread

server: crsd.c
	g++ -O3 -std=c++17 -o crsd crsd.c -lpthread

# Amazon Linux 2 currently has gcc 7.3.0, which means it only supports C++17
debug-client: crc.c
	g++ -g -w -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -o crc crc.c -lpthread

debug-server: crsd.c
	g++ -g -w -std=c++17 -fsanitize=address,undefined -fno-omit-frame-pointer -o crsd crsd.c -lpthread

clean:
	rm crsd crc
