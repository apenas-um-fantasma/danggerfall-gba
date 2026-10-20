# Requer o devkitARM (devkitPro) instalado e no caminho de comandos.
NOME = daggerfall
CC = arm-none-eabi-gcc
COPIA = arm-none-eabi-objcopy
OPCOES = -mthumb -mthumb-interwork -O2 -Wall -fno-strict-aliasing -specs=gba.specs

all: $(NOME).gba

arte.h: gerar_arte.py
	python3 gerar_arte.py

$(NOME).elf: main.c arte.h
	$(CC) $(OPCOES) main.c -o $@ -lm

$(NOME).gba: $(NOME).elf
	$(COPIA) -O binary $< $@
	gbafix $@

# Teste no computador: gera imagens de várias telas (q_*.png)
teste: main.c arte.h
	gcc -DTESTE_PC -O1 main.c -o teste_pc -lm
	./teste_pc
	python3 ver_quadros.py

limpar:
	rm -f *.elf *.gba teste_pc q_*.raw q_*.png
