# danggerfall-gba
# Daggerfall GBA (protótipo)

Jogo inspirado em The Elder Scrolls II: Daggerfall para Game Boy Advance, feito para rodar em emulador(não foi testado em hardware real e não recomendo por ser relativamente "pesado", porém, fica a seu critério).

## Como montar
1. Instale o devkitARM (devkitPro).
2. Na pasta do projeto, rode `make` (ele gera `arte.h` e depois `daggerfall.gba`).
3. Abra `daggerfall.gba` no mGBA.

## Controles
- Setas: andar e girar
- L / R: andar de lado
- A: ação (abrir a loja da aldeia, entrar na masmorra) e atacar
- Cima + A: estocada (alcance maior, mais dano)
- Esquerda ou Direita + A: talho (mais largo, menos dano)
- B: sair da masmorra (perto da porta)
- Select: beber uma poção (recupera 15 de vida)
- Start: menu (personagem, mapa, salvar, como jogar)

## O que tem
- Mundo procedural 3d com Ruído Perlin (lagos, areia, grama, montanhas, neve), desenhado como uma malha de triângulos com sombreamento pela inclinação (visual de polígonos).
- Só 64x64 células ficam na memória, ao redor do jogador.
- Árvores, aldeias e entradas de masmorra: figuras 2D de longe, figuras 3D de perto.
- Masmorras em 3D, com inimigos que perseguem e atacam.
- Combate em primeira pessoa, com fôlego, ouro e nível (os inimigos ficam mais fortes e mais numerosos com o seu nível).
- Loja em cada aldeia: descansar de graça, comprar poção (10 de ouro) e afiar a espada (até +5).
- Poções podem aparecer ao derrotar inimigos; limpar uma masmorra dá ouro de bônus.
- Salvamento na memória de bateria (SRAM) pelo menu, só no mundo aberto. O título mostra "Continuar".
- Objetivo: derrotar 30 inimigos para ver a tela de vitória (o jogo continua depois).

## Se ficar lento
No começo do trecho do relevo em `main.c` (linha 361) existe `#define ALC 28` (alcance da visão, em células). Diminuir esse número (por exemplo 15) reduz bastante a quantidade de triângulos por quadro o que, consequentemente, melhora o desempenho mas diminui a quantidade de polígonos visíveis envolta. O máximo é 31.

## Trocar as imagens
As figuras ficam em `imagens/` (PNG). Troque qualquer uma por outra feita por você ou por uma IA de imagens, mantendo o mesmo tamanho em pixels, e rode `make` de novo. Cada cor é trocada pela mais próxima da paleta do jogo. (em breve adicionarei no repositório)

## Teste no computador
`make teste` compila uma versão que gera imagens de várias telas, sem precisar do emulador.

## OBS
- Esse Demake é apenas um teste, não planejo fazer um jogo completo, porém, irei atualizar aos poucos para deixar o jogo mais polido.
