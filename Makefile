vzip: serial.c
	gcc serial.c -lz -lm -o vzip -pthread

test:
	rm -f video.vzip
	./vzip frames
	./check.sh

clean:
	rm -f vzip video.vzip