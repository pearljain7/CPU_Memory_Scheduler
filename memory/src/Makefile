all: compile

compile:
	gcc -g -Wall memory_coordinator.c -o memory_coordinator -lvirt -fsanitize=address

clean:
	rm memory_coordinator
