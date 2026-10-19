# Pruebas de la lógica de protección y telemetría en el PC (gcc normal, sin hardware)
CFLAGS = -std=c11 -Wall -Wextra -g -I. -I../inc
run: test_proteccion
	./test_proteccion
test_proteccion: test_proteccion.c ../src/protection.c ../src/telemetry.c board.h
	$(CC) $(CFLAGS) test_proteccion.c ../src/protection.c ../src/telemetry.c -o $@
clean:
	rm -f test_proteccion
.PHONY: run clean
