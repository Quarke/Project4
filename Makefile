CC = g++
CFLAGS = -O2 -Wall -std=gnu++11 -pthread -lcurl
TARGETS = main.o

sort : $(TARGETS)
	$(CC) $(CFLAGS) -o site-tester $(TARGETS)

%.o : %.cpp
	$(CC) $(CFLAGS) -c $<

clean:
	rm *.o
	rm site-tester
	rm *.html








