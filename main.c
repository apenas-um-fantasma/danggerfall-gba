/*
 * DAGGERFALL GBA - protótipo inspirado em The Elder Scrolls II: Daggerfall
 *
 *  - Mundo procedural com Ruído Perlin (montanhas, lagos, areia, neve)
 *  - Só uma janela pequena do mundo (64x64 células) fica na memória
 *  - Árvores, aldeias e entradas de masmorra: figuras 2D de longe,
 *    blocos em 3D quando o jogador chega perto
 *  - Masmorras em 3D (labirinto), com inimigos e combate em primeira pessoa
 *  - Menu com personagem, mapa e ajuda
 *
 * Tela interna de 120x80 pontos, ampliada 2x2 no modo gráfico 4 do GBA.
 */
#include <stdint.h>
#include <string.h>
#include <math.h>
#include "arte.h"

typedef uint8_t u8;
typedef int8_t s8;
typedef uint16_t u16;
typedef int16_t s16;
typedef uint32_t u32;
typedef int32_t s32;

/* ------------------------------------------------------------ equipamento */
#ifdef TESTE_PC
#include <stdio.h>
#include <stdlib.h>
static u16 mem_dispcnt, mem_vcont, mem_teclas = 0x3FF;
static u16 mem_paleta[256];
static u16 mem_vram[0x14000 / 2];
#define REG_DISPCNT mem_dispcnt
#define REG_VCONT mem_vcont
#define REG_TECLAS mem_teclas
#define MEM_PALETA mem_paleta
#define MEM_VRAM mem_vram
#define FUNC_IWRAM
#define DADOS_EWRAM
static u8 mem_sram[256];
#define MEM_SRAM mem_sram
#else
#define REG_DISPCNT (*(volatile u16 *)0x04000000)
#define REG_VCONT (*(volatile u16 *)0x04000006)
#define REG_TECLAS (*(volatile u16 *)0x04000130)
#define MEM_PALETA ((volatile u16 *)0x05000000)
#define MEM_VRAM ((volatile u16 *)0x06000000)
#define FUNC_IWRAM __attribute__((section(".iwram"), target("arm"), long_call, noinline))
#define DADOS_EWRAM __attribute__((section(".sbss")))
#define MEM_SRAM ((volatile u8 *)0x0E000000)
#endif

/* o emulador procura esta marca para ativar a memória de salvamento */
__attribute__((used)) static const char marca_sram[] = "SRAM_V113";

#define TC_A 1
#define TC_B 2
#define TC_SELECT 4
#define TC_START 8
#define TC_DIR 16
#define TC_ESQ 32
#define TC_CIMA 64
#define TC_BAIXO 128
#define TC_R 256
#define TC_L 512

/* ------------------------------------------------------------- constantes */
#define LARG 120
#define ALT_T 80
#define HORIZ 40
#define PROF 24          /* fatias de profundidade do relevo */
#define PERTO_3D 10      /* até esta distância, casas e entradas viram 3D */
#define JAN 64           /* lado da janela de células na memória */
#define MAX_OBJ 40
#define SEMENTE 7331
#define NIVEL_AGUA 31
#define LIM_ROCHA 58
#define LIM_NEVE 72
#define ESCALA_V 28

#define R_AGUA 0
#define R_GRAMA 3
#define R_CASA 9
#define R_TELHADO 10
#define R_PEDRA 11
#define R_TIJOLO_A 12
#define R_TIJOLO_B 13
#define R_CHAO 14
#define R_TETO 15
#define COR_RAMPA(r, l) (64 + (r) * 12 + (l))

#define ABS(x) ((x) < 0 ? -(x) : (x))

/* ---------------------------------------------------------------- memória */
static u8 fb[ALT_T][LARG] __attribute__((aligned(4)));
static s16 ys[PROF + 1][LARG];
static u8 cc[PROF + 1][LARG];
static s32 zbuf[LARG];

static u8 c_alt[JAN * JAN] DADOS_EWRAM;
static u8 c_tipo[JAN * JAN] DADOS_EWRAM;
static u8 c_obj[JAN * JAN] DADOS_EWRAM;
static u8 c_marca[JAN * JAN] DADOS_EWRAM;

typedef struct { s16 z, col, y; u8 tipo; } Objeto;
static Objeto objs[MAX_OBJ];
static int nobj;

static s16 sen_t[256];
static s16 camx_t[LARG];
static u8 nevoa_t[PROF + 1];
static s32 mul_t[PROF + 1];

#define SEN(a) sen_t[(a) & 255]
#define COS(a) sen_t[((a) + 64) & 255]

/* ------------------------------------------------------- números aleatórios */
static u32 rng_estado = 12345;
static u32 aleatorio(void) {
    rng_estado = rng_estado * 1664525u + 1013904223u;
    return rng_estado >> 16;
}

static u32 hash2(s32 x, s32 y) {
    u32 h = (u32)x * 374761393u + (u32)y * 668265263u + 0x9E3779B9u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* -------------------------------------------------------------- Ruído Perlin */
static s32 grad(u32 h, s32 dx, s32 dy) {
    switch (h & 7) {
    case 0: return dx + dy;
    case 1: return dx - dy;
    case 2: return -dx + dy;
    case 3: return -dx - dy;
    case 4: return dx;
    case 5: return -dx;
    case 6: return dy;
    default: return -dy;
    }
}

static s32 suave(s32 t) { return (t * t * (768 - 2 * t)) >> 16; }

/* x e y em ponto fixo 8.8 (coordenadas da grade); resultado em torno de +-300 */
static s32 ruido(s32 x, s32 y) {
    s32 ix = x >> 8, iy = y >> 8, fx = x & 255, fy = y & 255;
    s32 u = suave(fx), v = suave(fy);
    s32 a = grad(hash2(ix, iy), fx, fy);
    s32 b = grad(hash2(ix + 1, iy), fx - 256, fy);
    s32 c = grad(hash2(ix, iy + 1), fx, fy - 256);
    s32 d = grad(hash2(ix + 1, iy + 1), fx - 256, fy - 256);
    s32 l1 = a + (((b - a) * u) >> 8);
    s32 l2 = c + (((d - c) * u) >> 8);
    return l1 + (((l2 - l1) * v) >> 8);
}

static int altura_bruta(s32 x, s32 y) {
    x += SEMENTE;
    y += SEMENTE * 3 + 17;
    s32 n = ruido(x * 8, y * 8) * 4 + ruido(x * 16 + 4096, y * 16 + 4096) * 2 +
            ruido(x * 32 + 9000, y * 32 + 9000);
    int a = 44 + n / 20;
    if (a < 1) a = 1;
    if (a > 95) a = 95;
    return a;
}

/* ------------------------------------------------- aldeias e masmorras */
typedef struct { int tipo; s32 cx, cy; int base; } Estrutura;
static s32 eb_x = 0x7FFFFFFF, eb_y;
static Estrutura eb;

/* cada bloco de 32x32 células pode ter uma aldeia (1) ou uma masmorra (2) */
static Estrutura *estrutura_bloco(s32 bx, s32 by) {
    if (bx == eb_x && by == eb_y) return &eb;
    eb_x = bx;
    eb_y = by;
    u32 h = hash2(bx * 31 + SEMENTE, by * 17 + 7);
    int sel = h & 3;
    eb.tipo = 0;
    if (sel < 2) {
        eb.cx = bx * 32 + 8 + ((h >> 4) & 15);
        eb.cy = by * 32 + 8 + ((h >> 9) & 15);
        eb.base = altura_bruta(eb.cx, eb.cy);
        if (eb.base >= NIVEL_AGUA + 3 && eb.base < LIM_ROCHA) eb.tipo = (sel == 0) ? 1 : 2;
    }
    return &eb;
}

/* tipos de célula: 0 água, 1 areia, 2 grama, 3 rocha, 4 neve
 * objetos: 1 árvore, 2 aldeia, 3 entrada, 4 parede de casa, 5 telhado,
 *          6 pedra da entrada, 7 água (bloqueia) */
static const u8 rampa_tipo[5] = {0, 1, 3, 5, 7};

static void gerar_celula(s32 wx, s32 wy) {
    int i = ((wy & 63) << 6) | (wx & 63);
    int a = altura_bruta(wx, wy);
    int tipo, obj = 0, dentro = 0;
    s32 dx = 0, dy = 0;
    Estrutura *e = estrutura_bloco(wx >> 5, wy >> 5);
    if (e->tipo) {
        dx = wx - e->cx;
        dy = wy - e->cy;
        int raio = (e->tipo == 1) ? 6 : 3;
        if (ABS(dx) <= raio && ABS(dy) <= raio) {
            a = e->base;
            dentro = 1;
        }
    }
    if (a < NIVEL_AGUA) { a = NIVEL_AGUA; tipo = 0; obj = 7; }
    else if (a < NIVEL_AGUA + 3) tipo = 1;
    else if (a < LIM_ROCHA) tipo = 2;
    else if (a < LIM_NEVE) tipo = 3;
    else tipo = 4;

    if (dentro) {
        if (e->tipo == 1) {
            static const s8 hx[4] = {-3, 3, -3, 3}, hy[4] = {-3, -3, 3, 3};
            tipo = (ABS(dx) <= 1 && ABS(dy) <= 1) ? 1 : 2;
            for (int k = 0; k < 4; k++) {
                s32 ax = dx - hx[k], ay = dy - hy[k];
                if (ABS(ax) <= 1 && ABS(ay) <= 1) obj = (ax == 0 && ay == 0) ? 5 : 4;
            }
            if (dx == 0 && dy == 0) obj = 2;
        } else {
            tipo = 3;
            if (dx == 0 && dy == 0) obj = 3;
            else if (ABS(dx) <= 1 && ABS(dy) <= 1 && !(dx == 0 && dy == 1)) obj = 6;
        }
    } else if (tipo == 2 && (hash2(wx * 3 + 5, wy * 7 + 9) & 15) == 0) {
        obj = 1;
    }
    c_alt[i] = (u8)a;
    c_tipo[i] = (u8)tipo;
    c_obj[i] = (u8)obj;
}

static s32 jan_cx, jan_cy;
static int jan_valida;

static void janela_atualizar(s32 cx, s32 cy) {
    s32 x, y;
    if (!jan_valida || ABS(cx - jan_cx) > 8 || ABS(cy - jan_cy) > 8) {
        jan_cx = cx;
        jan_cy = cy;
        jan_valida = 1;
        for (y = cy - 32; y < cy + 32; y++)
            for (x = cx - 32; x < cx + 32; x++) gerar_celula(x, y);
        return;
    }
    while (jan_cx < cx) {
        jan_cx++;
        x = jan_cx + 31;
        for (y = jan_cy - 32; y < jan_cy + 32; y++) gerar_celula(x, y);
    }
    while (jan_cx > cx) {
        jan_cx--;
        x = jan_cx - 32;
        for (y = jan_cy - 32; y < jan_cy + 32; y++) gerar_celula(x, y);
    }
    while (jan_cy < cy) {
        jan_cy++;
        y = jan_cy + 31;
        for (x = jan_cx - 32; x < jan_cx + 32; x++) gerar_celula(x, y);
    }
    while (jan_cy > cy) {
        jan_cy--;
        y = jan_cy - 32;
        for (x = jan_cx - 32; x < jan_cx + 32; x++) gerar_celula(x, y);
    }
}

static int idx_cel(s32 cx, s32 cy) { return ((cy & 63) << 6) | (cx & 63); }

/* ---------------------------------------------------------------- desenho */
static void retangulo(int x, int y, int w, int h, u8 cor) {
    for (int j = y; j < y + h; j++) {
        if (j < 0 || j >= ALT_T) continue;
        for (int i = x; i < x + w; i++)
            if (i >= 0 && i < LARG) fb[j][i] = cor;
    }
}

static void caixa(int x, int y, int w, int h) {
    retangulo(x, y, w, h, COR_DOURADO);
    retangulo(x + 1, y + 1, w - 2, h - 2, COR_PAINEL);
}

static const u8 *glifo(u32 cp) {
    if (cp >= 0xE0 && cp <= 0xFF) cp -= 0x20;
    if (cp >= 32 && cp < 128) {
        if (cp >= 'a' && cp <= 'z') cp -= 32;
        return FONTE[cp - 32];
    }
    for (int i = 0; i < N_ACENTOS; i++)
        if (ACENTOS_CP[i] == cp) return FONTE_ACENTO[i];
    return FONTE['?' - 32];
}

static void glifo_pintar(int x, int y, const u8 *g, u8 cor) {
    for (int r = 0; r < 8; r++) {
        int py = y + r;
        if (py < 0 || py >= ALT_T) continue;
        for (int c = 0; c < 5; c++) {
            int px = x + c;
            if (px >= 0 && px < LARG && (g[r] & (0x80 >> c))) fb[py][px] = cor;
        }
    }
}

static int larg_texto(const char *s) {
    int n = 0;
    for (; *s; s++)
        if (((u8)*s & 0xC0) != 0x80) n++;
    return n * 6;
}

static void texto(int x, int y, const char *s, u8 cor) {
    const u8 *p = (const u8 *)s;
    while (*p) {
        u32 cp = *p++;
        if (cp >= 0xC0 && cp < 0xE0 && *p) cp = ((cp & 0x1F) << 6) | (*p++ & 0x3F);
        const u8 *g = glifo(cp);
        glifo_pintar(x + 1, y + 1, g, 0);
        glifo_pintar(x, y, g, cor);
        x += 6;
    }
}

static void texto_centro(int y, const char *s, u8 cor) {
    texto((LARG - larg_texto(s)) / 2, y, s, cor);
}

static void numero_str(int v, char *buf) {
    char tmp[12];
    int n = 0, neg = 0;
    if (v < 0) { neg = 1; v = -v; }
    do { tmp[n++] = '0' + (v % 10); v /= 10; } while (v);
    int k = 0;
    if (neg) buf[k++] = '-';
    while (n) buf[k++] = tmp[--n];
    buf[k] = 0;
}

static void texto_num(int x, int y, const char *rot, int v, u8 cor) {
    char buf[24];
    numero_str(v, buf);
    texto(x, y, rot, cor);
    texto(x + larg_texto(rot), y, buf, COR_BRANCO);
}

/* figura 2D redimensionada; se 'zb' não for nulo, testa a profundidade por coluna */
static void figura_escalada(const u8 *img, int sw, int sh, int x0, int y0, int dw, int dh,
                            const s32 *zb, s32 prof) {
    if (dw < 1 || dh < 1) return;
    s32 su = (sw << 16) / dw, sv = (sh << 16) / dh;
    int dx0 = x0 < 0 ? -x0 : 0, dx1 = (x0 + dw > LARG) ? LARG - x0 : dw;
    int dy0 = y0 < 0 ? -y0 : 0, dy1 = (y0 + dh > ALT_T) ? ALT_T - y0 : dh;
    for (int dx = dx0; dx < dx1; dx++) {
        int sx = (dx * su) >> 16, X = x0 + dx;
        if (zb && zb[X] < prof) continue;
        for (int dy = dy0; dy < dy1; dy++) {
            u8 p = img[(((dy * sv) >> 16)) * sw + sx];
            if (p) fb[y0 + dy][X] = p;
        }
    }
}

/* ------------------------------------------------------ relevo (mundo 3D) */
/*
 * Amostra a altura do terreno em ponto fixo 8.8, interpolando entre as
 * quatro células vizinhas. A malha lógica continua em células inteiras para
 * colisões/estruturas, mas a superfície visual deixa de formar degraus largos.
 */
static s32 altura_interpolada(u32 xu, u32 yu) {
    s32 wx = (s32)xu, wy = (s32)yu;
    s32 cx = wx >> 16, cy = wy >> 16;
    int fx = (int)((xu >> 8) & 255), fy = (int)((yu >> 8) & 255);
    int i00 = idx_cel(cx, cy);
    int i10 = idx_cel(cx + 1, cy);
    int i01 = idx_cel(cx, cy + 1);
    int i11 = idx_cel(cx + 1, cy + 1);
    s32 h00 = (s32)c_alt[i00] << 8;
    s32 h10 = (s32)c_alt[i10] << 8;
    s32 h01 = (s32)c_alt[i01] << 8;
    s32 h11 = (s32)c_alt[i11] << 8;
    s32 a = h00 + (((h10 - h00) * fx) >> 8);
    s32 b = h01 + (((h11 - h01) * fx) >> 8);
    return a + (((b - a) * fy) >> 8);
}

static void desenhar_objeto(const Objeto *o) {
    const u8 *img;
    int sw, sh;
    if (o->tipo == 1) { img = FIG_ARVORE; sw = ARVORE_L; sh = ARVORE_A; }
    else if (o->tipo == 2) { img = FIG_ALDEIA; sw = ALDEIA_L; sh = ALDEIA_A; }
    else { img = FIG_MASMORRA; sw = MASMORRA_L; sh = MASMORRA_A; }
    int esc = (5 * 256) / o->z;
    int dw = (sw * esc) >> 8, dh = (sh * esc) >> 8;
    if (dw < 1) dw = 1;
    if (dh < 1) dh = 1;
    figura_escalada(img, sw, sh, o->col - dw / 2, o->y - dh, dw, dh, 0, 0);
}

FUNC_IWRAM static void desenhar_relevo(s32 px, s32 py, int ang, s32 camh) {
    int z, col, y;
    for (y = 0; y < HORIZ; y++) memset(fb[y], COR_CEU0 + (y * COR_CEU_N) / HORIZ, LARG);
    for (; y < ALT_T; y++) memset(fb[y], COR_RAMPA(R_GRAMA, 11), LARG);
    memset(c_marca, 0, sizeof c_marca);
    nobj = 0;
    s32 c = COS(ang), s = SEN(ang);

    /* passo 1: projeta o relevo, da fatia mais longe para a mais perto */
    for (z = PROF; z >= 2; z--) {
        int L = nevoa_t[z];
        s32 mul = mul_t[z], d3 = z * 3;
        u32 x = (u32)(px + z * c * 256 + d3 * s * 64);
        u32 yy = (u32)(py + z * s * 256 - d3 * c * 64);
        s32 sx = -(d3 * s * 128) / LARG, sy = (d3 * c * 128) / LARG;
        for (col = 0; col < LARG; col++, x += (u32)sx, yy += (u32)sy) {
            s32 wx = (s32)x, wy = (s32)yy;
            s32 cx = wx >> 16, cy = wy >> 16;
            int i = (int)((((yy >> 16) & 63) << 6) | ((x >> 16) & 63));
            int f = c_obj[i], ramp, tone = L;
            s32 hfix = altura_interpolada(x, yy);
            if (z <= PERTO_3D && f >= 4 && f <= 6) {
                /* Casas e pedras ganham volume 3D apenas quando estão perto. */
                hfix = (s32)c_alt[i] << 8;
                if (f == 4) { hfix += 4 * 256; ramp = R_CASA; }
                else if (f == 5) { hfix += 7 * 256; ramp = R_TELHADO; }
                else { hfix += 6 * 256; ramp = R_PEDRA; }
            } else {
                /* Biomas seguem a altura interpolada, sem bordas em blocos. */
                int ah = (int)(hfix >> 8), t;
                /* Preserva o piso de praças e o solo rochoso de masmorras,
                 * que são regiões planas definidas pela lógica das estruturas. */
                if (c_tipo[i] == 1 && c_alt[i] >= NIVEL_AGUA + 3 && c_alt[i] < LIM_ROCHA) t = 1;
                else if (c_tipo[i] == 3 && c_alt[i] >= NIVEL_AGUA + 3 && c_alt[i] < LIM_ROCHA) t = 3;
                else if (ah < NIVEL_AGUA ||
                         (c_tipo[i] == 0 && hfix <= ((NIVEL_AGUA + 1) << 8))) t = 0;
                else if (ah < NIVEL_AGUA + 3) t = 1;
                else if (ah < LIM_ROCHA) t = 2;
                else if (ah < LIM_NEVE) t = 3;
                else t = 4;
                ramp = rampa_tipo[t];

                /* Iluminação direcional aproximada pelas inclinações locais. */
                int gx = (int)c_alt[idx_cel(cx + 1, cy)] - (int)c_alt[idx_cel(cx - 1, cy)];
                int gy = (int)c_alt[idx_cel(cx, cy + 1)] - (int)c_alt[idx_cel(cx, cy - 1)];
                int shade = (gx - gy) / 8;
                if (shade < -1) shade = -1;
                if (shade > 1) shade = 1;
                /* Contraste discreto: conserva a paleta e evita faixas duras. */
                tone += (t == 0) ? 0 : shade;
                if (tone < 0) tone = 0;
                if (tone > 11) tone = 11;
            }
            int cor = COR_RAMPA(ramp, tone);
            if (z <= PERTO_3D && f == 3) cor = COR_ENTRADA;
            int yp = HORIZ + (int)(((camh - hfix) * mul) >> 16);
            if (yp < -1) yp = -1;
            if (yp > ALT_T) yp = ALT_T;
            ys[z][col] = (s16)yp;
            cc[z][col] = (u8)cor;
            if (f && f < 4 && !c_marca[i] && nobj < MAX_OBJ && (f == 1 ? z >= 2 : z > PERTO_3D)) {
                c_marca[i] = 1;
                objs[nobj].z = (s16)z;
                objs[nobj].col = (s16)col;
                objs[nobj].y = (s16)yp;
                objs[nobj].tipo = (u8)f;
                nobj++;
            }
        }
    }

    /* passo 2: pinta as colunas e as figuras 2D, de longe para perto */
    int oi = 0;
    for (z = PROF; z >= 2; z--) {
        for (col = 0; col < LARG; col++) {
            int yt = ys[z][col];
            int yb = (z > 2) ? ys[z - 1][col] : ALT_T;
            if (yb <= yt) continue;
            if (yt < 0) yt = 0;
            u8 cor = cc[z][col];
            for (y = yt; y < yb; y++) fb[y][col] = cor;
        }
        while (oi < nobj && objs[oi].z == z) desenhar_objeto(&objs[oi++]);
    }
}

/* ---------------------------------------------------------------- jogador */
enum { MODO_MUNDO, MODO_MASMORRA };
enum { ST_TITULO, ST_JOGO, ST_MENU, ST_PERSONAGEM, ST_MAPA, ST_AJUDA, ST_MORTE, ST_LOJA, ST_VITORIA };

static int estado, modo, menu_sel, menu_redesenhar, pagina_origem;
static int vida, vida_max, folego, folego_max, ouro, xp, nivel, mortos;
static s32 wpx, wpy, wcamh;
static int wang;
static const char *aviso_txt;
static int aviso_t;
static int dano_flash;
static int anim_andar;
static s32 entrada_x, entrada_y;
static int mapa_pronto;
static int pocoes, espada, venceu, masm_limpa, tem_save, loja_sel;
#define META_INIMIGOS 30
#define ESPADA_MAX 5
#define CUSTO_POCAO 10

static int nivel_de(int exp) {
    int n = 1 + exp / 30;
    return n > 30 ? 30 : n;
}

static u8 sav_buf[32];
static int sav_n;
static void sav_poe(int bytes, u32 v) {
    for (int i = 0; i < bytes; i++) sav_buf[sav_n++] = (u8)(v >> (8 * i));
}
static u32 sav_pega(int bytes) {
    u32 v = 0;
    for (int i = 0; i < bytes; i++) v |= (u32)sav_buf[sav_n++] << (8 * i);
    return v;
}
FUNC_IWRAM static void sram_gravar(const u8 *o, int n) {
    for (int i = 0; i < n; i++) MEM_SRAM[i] = o[i];
}
FUNC_IWRAM static void sram_ler(u8 *d, int n) {
    for (int i = 0; i < n; i++) d[i] = MEM_SRAM[i];
}
#define SAV_TAM 25

static void salvar_jogo(void) {
    sav_n = 0;
    sav_poe(1, 'D'); sav_poe(1, 'F'); sav_poe(1, 1);
    sav_poe(1, nivel); sav_poe(2, xp); sav_poe(2, ouro); sav_poe(2, mortos);
    sav_poe(1, vida); sav_poe(1, folego); sav_poe(1, pocoes); sav_poe(1, espada);
    sav_poe(4, (u32)wpx); sav_poe(4, (u32)wpy); sav_poe(1, wang & 255);
    sav_poe(1, venceu);
    u8 soma = 0;
    for (int i = 0; i < sav_n; i++) soma = (u8)(soma + sav_buf[i]);
    sav_poe(1, soma);
    sram_gravar(sav_buf, sav_n);
    tem_save = 1;
}

static int carregar_ok(void) {
    sram_ler(sav_buf, SAV_TAM);
    if (sav_buf[0] != 'D' || sav_buf[1] != 'F' || sav_buf[2] != 1) return 0;
    u8 soma = 0;
    for (int i = 0; i < SAV_TAM - 1; i++) soma = (u8)(soma + sav_buf[i]);
    return soma == sav_buf[SAV_TAM - 1];
}

static void aviso(const char *t) { aviso_txt = t; aviso_t = 90; }

static void beber_pocao(void) {
    if (pocoes <= 0) { aviso("SEM POÇÕES!"); return; }
    if (vida >= vida_max) { aviso("VIDA JÁ ESTÁ CHEIA."); return; }
    pocoes--;
    vida += 15;
    if (vida > vida_max) vida = vida_max;
    aviso("VOCÊ BEBEU A POÇÃO.");
}

static int bloqueado_mundo(s32 x, s32 y) {
    int f = c_obj[idx_cel(x >> 16, y >> 16)];
    return f == 1 || f >= 4;
}

static s32 altura_camera(void) {
    s32 pcx = wpx >> 16, pcy = wpy >> 16;
    int m = 0;
    for (s32 y = pcy - 2; y <= pcy + 2; y++)
        for (s32 x = pcx - 2; x <= pcx + 2; x++) {
            int a = c_alt[idx_cel(x, y)];
            if (a > m) m = a;
        }
    return (m + 6) * 256;
}

static void encontrar_aldeia(s32 *ox, s32 *oy) {
    for (int r = 0; r <= 6; r++)
        for (int by = -r; by <= r; by++)
            for (int bx = -r; bx <= r; bx++) {
                if (ABS(bx) != r && ABS(by) != r) continue;
                Estrutura *e = estrutura_bloco(bx, by);
                if (e->tipo == 1) { *ox = e->cx; *oy = e->cy; return; }
            }
    *ox = 0; *oy = 0;
}

static void ir_para_aldeia_inicial(void) {
    s32 ax, ay;
    encontrar_aldeia(&ax, &ay);
    wpx = (ax << 16) + 32768;
    wpy = ((ay + 6) << 16) + 32768;
    wang = 192;
    jan_valida = 0;
    janela_atualizar(wpx >> 16, wpy >> 16);
    wcamh = altura_camera();
}

static void carregar_jogo(void) {
    if (!carregar_ok()) return;
    sav_n = 3;
    nivel = (int)sav_pega(1); xp = (int)sav_pega(2); ouro = (int)sav_pega(2);
    mortos = (int)sav_pega(2); vida = (int)sav_pega(1); folego = (int)sav_pega(1);
    pocoes = (int)sav_pega(1); espada = (int)sav_pega(1);
    wpx = (s32)sav_pega(4); wpy = (s32)sav_pega(4); wang = (int)sav_pega(1);
    venceu = (int)sav_pega(1);
    if (nivel < 1) nivel = 1;
    vida_max = 20 + 5 * (nivel - 1);
    folego_max = 30;
    if (vida > vida_max) vida = vida_max;
    modo = MODO_MUNDO;
    jan_valida = 0;
    janela_atualizar(wpx >> 16, wpy >> 16);
    wcamh = altura_camera();
}

static void jogo_novo(void) {
    nivel = 1; xp = 0; ouro = 10; mortos = 0;
    pocoes = 1; espada = 0; venceu = 0;
    vida_max = 20; vida = vida_max;
    folego_max = 30; folego = folego_max;
    modo = MODO_MUNDO;
    ir_para_aldeia_inicial();
}

/* ---------------------------------------------------------------- masmorra */
#define MW 17
#define MH 17
#define MAX_INI 8
#define VEL_ANDAR 28

typedef struct { s32 x, y; int vida; u8 vivo, espera, flash, anim; } Inimigo;
static u8 masm[MH][MW];
static Inimigo ini[MAX_INI];
static int nini;
static s32 mpx, mpy;
static int mang;
static int atq_t, atq_tipo, atq_acertou;

static void gerar_masmorra(u32 semente) {
    static const s8 d2x[4] = {2, -2, 0, 0}, d2y[4] = {0, 0, 2, -2};
    int pilha[64][2], sp = 0, k;
    rng_estado = semente;
    memset(masm, 1, sizeof masm);
    masm[1][1] = 0;
    pilha[0][0] = 1; pilha[0][1] = 1; sp = 1;
    while (sp) {
        int cx = pilha[sp - 1][0], cy = pilha[sp - 1][1];
        int dirs[4] = {0, 1, 2, 3}, ok = 0;
        for (k = 3; k > 0; k--) {
            int j = aleatorio() % (k + 1), t = dirs[k];
            dirs[k] = dirs[j]; dirs[j] = t;
        }
        for (k = 0; k < 4; k++) {
            int d = dirs[k], nx = cx + d2x[d], ny = cy + d2y[d];
            if (nx > 0 && nx < MW - 1 && ny > 0 && ny < MH - 1 && masm[ny][nx]) {
                masm[cy + d2y[d] / 2][cx + d2x[d] / 2] = 0;
                masm[ny][nx] = 0;
                pilha[sp][0] = nx; pilha[sp][1] = ny; sp++;
                ok = 1;
                break;
            }
        }
        if (!ok) sp--;
    }
    for (k = 0; k < 16; k++) {      /* aberturas extras: cria ciclos */
        int x = 1 + aleatorio() % (MW - 2), y = 1 + aleatorio() % (MH - 2);
        if ((x & 1) != (y & 1)) masm[y][x] = 0;
    }
    mpx = 256 + 128; mpy = 256 + 128;
    mang = masm[1][2] ? 64 : 0;
    nini = 0;
    int alvo_ini = 4 + nivel / 2;
    if (alvo_ini > 7) alvo_ini = 7;
    masm_limpa = 0;
    for (k = 0; k < 90 && nini < alvo_ini; k++) {
        int x = 1 + 2 * (aleatorio() % 8), y = 1 + 2 * (aleatorio() % 8);
        if (ABS(x - 1) + ABS(y - 1) < 6) continue;
        int dup = 0;
        for (int j = 0; j < nini; j++)
            if ((ini[j].x >> 8) == x && (ini[j].y >> 8) == y) dup = 1;
        if (dup) continue;
        ini[nini].x = x * 256 + 128; ini[nini].y = y * 256 + 128;
        ini[nini].vida = 14 + (nivel < 16 ? 2 * (nivel - 1) : 30); ini[nini].vivo = 1;
        ini[nini].espera = 0; ini[nini].flash = 0; ini[nini].anim = aleatorio() & 31;
        nini++;
    }
    atq_t = 0; atq_tipo = 0; atq_acertou = 0;
}

static int livre(s32 x, s32 y, int r) {
    return !masm[(y - r) >> 8][(x - r) >> 8] && !masm[(y - r) >> 8][(x + r) >> 8] &&
           !masm[(y + r) >> 8][(x - r) >> 8] && !masm[(y + r) >> 8][(x + r) >> 8];
}

FUNC_IWRAM static void desenhar_masmorra(void) {
    int col, y;
    s32 c = COS(mang), s = SEN(mang);
    s32 planex = (-s * 169) >> 8, planey = (c * 169) >> 8;
    for (y = 0; y < HORIZ; y++) memset(fb[y], COR_RAMPA(R_TETO, 3 + (y * 8) / HORIZ), LARG);
    for (y = HORIZ; y < ALT_T; y++)
        memset(fb[y], COR_RAMPA(R_CHAO, 10 - ((y - HORIZ) * 8) / HORIZ), LARG);

    for (col = 0; col < LARG; col++) {
        s32 camx = camx_t[col];
        s32 rdx = c + ((planex * camx) >> 8), rdy = s + ((planey * camx) >> 8);
        int mx = mpx >> 8, my = mpy >> 8, stepx, stepy, lado = 0;
        s32 ddx = rdx ? ABS(65536 / rdx) : 0x400000;
        s32 ddy = rdy ? ABS(65536 / rdy) : 0x400000;
        s32 sdx, sdy;
        if (rdx < 0) { stepx = -1; sdx = ((mpx & 255) * ddx) >> 8; }
        else { stepx = 1; sdx = ((256 - (mpx & 255)) * ddx) >> 8; }
        if (rdy < 0) { stepy = -1; sdy = ((mpy & 255) * ddy) >> 8; }
        else { stepy = 1; sdy = ((256 - (mpy & 255)) * ddy) >> 8; }
        while (!masm[my][mx]) {
            if (sdx < sdy) { sdx += ddx; mx += stepx; lado = 0; }
            else { sdy += ddy; my += stepy; lado = 1; }
        }
        s32 perp = lado == 0 ? sdx - ddx : sdy - ddy;
        if (perp < 40) perp = 40;
        zbuf[col] = perp;
        s32 linh = (ALT_T * 256) / perp;
        int y0 = HORIZ - linh / 2;
        int ya = y0 < 0 ? 0 : y0, yb = y0 + linh > ALT_T ? ALT_T : y0 + linh;
        s32 wf = lado == 0 ? mpy + ((perp * rdy) >> 8) : mpx + ((perp * rdx) >> 8);
        int u = (wf & 255) >> 4;
        int L = (perp * 11) >> 11;
        if (L > 11) L = 11;
        int ramp = lado ? R_TIJOLO_B : R_TIJOLO_A;
        s32 vpasso = (16 << 16) / linh, v = (ya - y0) * vpasso;
        for (y = ya; y < yb; y++) {
            int row = (v >> 16) & 15;
            v += vpasso;
            int mort = ((row & 3) == 0) || (((u + ((row >> 2) & 1) * 8) & 15) == 0);
            int l2 = mort ? (L + 3 > 11 ? 11 : L + 3) : L;
            fb[y][col] = COR_RAMPA(ramp, l2);
        }
    }
}

static void projetar(s32 ex, s32 ey, s32 *tx, s32 *ty) {
    s32 c = COS(mang), s = SEN(mang);
    s32 planex = (-s * 169) >> 8, planey = (c * 169) >> 8;
    s32 det = planex * s - c * planey;
    s32 dx = ex - mpx, dy = ey - mpy;
    *tx = ((s * dx - c * dy) * 256) / det;
    *ty = ((-planey * dx + planex * dy) * 256) / det;
}

static void desenhar_inimigos(void) {
    int ordem[MAX_INI], n = 0, i, j;
    s32 dist[MAX_INI];
    for (i = 0; i < nini; i++) {
        if (!ini[i].vivo) continue;
        s32 tx, ty;
        projetar(ini[i].x, ini[i].y, &tx, &ty);
        dist[i] = ty;
        for (j = n; j > 0 && dist[ordem[j - 1]] < ty; j--) ordem[j] = ordem[j - 1];
        ordem[j] = i;
        n++;
    }
    for (i = 0; i < n; i++) {
        Inimigo *e = &ini[ordem[i]];
        s32 tx, ty;
        projetar(e->x, e->y, &tx, &ty);
        if (ty < 64) continue;
        int sxc = LARG / 2 + (60 * tx) / ty;
        s32 linh = (ALT_T * 256) / ty;
        int dh = (linh * 3) / 4, dw = (dh * 3) / 4;
        int ybase = HORIZ + linh / 2;
        int quadro = e->flash ? 2 : ((e->anim >> 3) & 1);
        figura_escalada(FIG_INIMIGO[quadro], INIMIGO_L, INIMIGO_A, sxc - dw / 2, ybase - dh, dw,
                        dh, zbuf, ty);
    }
}

static void desenhar_arma(void) {
    int f = 0, ox = 0, oy = 0;
    if (atq_t > 0) {
        static const u8 giro[3][10] = {{1, 1, 2, 2, 3, 3, 2, 2, 1, 0},
                                       {4, 4, 4, 4, 4, 4, 4, 1, 1, 0},
                                       {3, 3, 2, 2, 1, 1, 2, 2, 3, 0}};
        f = giro[atq_tipo][atq_t - 1];
        if (atq_tipo == 1) { ox = -4; oy = -(atq_t < 6 ? atq_t : 6) / 2; }
    } else if (anim_andar) {
        oy = (anim_andar & 8) ? 1 : 0;
        ox = ((anim_andar >> 3) & 1) ? 1 : -1;
    }
    figura_escalada(FIG_ESPADA[f], ESPADA_L, ESPADA_A, 70 + ox, 40 + oy, ESPADA_L, ESPADA_A, 0, 0);
}

static void atacar(void) {
    static const int alcance[3] = {410, 486, 333}, arco[3] = {90, 55, 160};
    static const int dano[3] = {5, 9, 4};
    int t = atq_tipo;
    for (int i = 0; i < nini; i++) {
        Inimigo *e = &ini[i];
        if (!e->vivo) continue;
        s32 tx, ty;
        projetar(e->x, e->y, &tx, &ty);
        if (ty <= 0 || ty > alcance[t] || ABS(tx) > ((ty * arco[t]) >> 8) + 48) continue;
        e->vida -= dano[t] + espada + nivel / 3;
        e->flash = 6;
        s32 dx = e->x - mpx, dy = e->y - mpy, n = ABS(dx) + ABS(dy) + 1;
        s32 nx = e->x + (dx * 80) / n, ny = e->y + (dy * 80) / n;
        if (livre(nx, ny, 40)) { e->x = nx; e->y = ny; }
        if (e->vida <= 0) {
            e->vivo = 0;
            mortos++;
            ouro += 5 + (int)(aleatorio() % 11);
            xp += 10;
            if (xp > 60000) xp = 60000;
            if (ouro > 9999) ouro = 9999;
            if ((aleatorio() & 3) == 0 && pocoes < 9) { pocoes++; aviso("ACHOU UMA POÇÃO!"); }
            int nv = nivel_de(xp);
            if (nv > nivel) {
                nivel = nv;
                vida_max = 20 + 5 * (nivel - 1);
                vida = vida_max;
                aviso("SUBIU DE NÍVEL!");
            }
            int vivos = 0;
            for (int j = 0; j < nini; j++) vivos += ini[j].vivo;
            if (!vivos && !masm_limpa) {
                masm_limpa = 1;
                ouro += 20 + 5 * nivel;
                if (ouro > 9999) ouro = 9999;
                aviso("MASMORRA LIMPA! +OURO");
            }
            if (mortos >= META_INIMIGOS && !venceu) { venceu = 1; estado = ST_VITORIA; }
        }
    }
}

static void atualizar_masmorra(u16 t, u16 n) {
    static const int custo[3] = {8, 14, 10};
    s32 c = COS(mang), s = SEN(mang);
    s32 nx, ny;
    anim_andar = 0;
    if (t & TC_ESQ) mang -= 3;
    if (t & TC_DIR) mang += 3;
    if (t & (TC_CIMA | TC_BAIXO)) {
        int sg = (t & TC_CIMA) ? 1 : -1;
        nx = mpx + sg * ((c * VEL_ANDAR) >> 8);
        ny = mpy + sg * ((s * VEL_ANDAR) >> 8);
        if (livre(nx, mpy, 50)) mpx = nx;
        if (livre(mpx, ny, 50)) mpy = ny;
        anim_andar = 1 + (aleatorio() & 15);
    }
    if (t & (TC_L | TC_R)) {
        int sg = (t & TC_R) ? 1 : -1;
        nx = mpx + sg * ((-s * VEL_ANDAR) >> 8);
        ny = mpy + sg * ((c * VEL_ANDAR) >> 8);
        if (livre(nx, mpy, 50)) mpx = nx;
        if (livre(mpx, ny, 50)) mpy = ny;
    }
    if (atq_t > 0) {
        atq_t++;
        if (atq_t == 5) atacar();
        if (atq_t > 10) atq_t = 0;
    } else if (n & TC_A) {
        int tipo = (t & TC_CIMA) ? 1 : ((t & (TC_ESQ | TC_DIR)) ? 2 : 0);
        if (folego >= custo[tipo]) {
            folego -= custo[tipo];
            atq_tipo = tipo;
            atq_t = 1;
        } else {
            aviso("SEM FÔLEGO!");
        }
    }
    static u8 reg = 0;
    if (++reg >= 4) { reg = 0; if (folego < folego_max) folego++; }

    for (int i = 0; i < nini; i++) {
        Inimigo *e = &ini[i];
        if (!e->vivo) continue;
        e->anim++;
        if (e->flash) e->flash--;
        if (e->espera) e->espera--;
        s32 dx = mpx - e->x, dy = mpy - e->y, dist = ABS(dx) + ABS(dy);
        if (dist < 9 * 256 && dist > 150) {
            s32 sn = dist + 1;
            s32 ex = e->x + (dx * 13) / sn, ey = e->y + (dy * 13) / sn;
            if (livre(ex, e->y, 40)) e->x = ex;
            if (livre(e->x, ey, 40)) e->y = ey;
        }
        if (dist < 200 && e->espera == 0) {
            vida -= 3 + (nivel < 16 ? nivel / 4 : 4) + (int)(aleatorio() % 3);
            e->espera = 40;
            dano_flash = 6;
        }
    }
    if (vida <= 0) { vida = 0; estado = ST_MORTE; }
    /* B perto da porta: sair */
    if (ABS(mpx - 384) < 300 && ABS(mpy - 384) < 300) {
        aviso_txt = "B: SAIR DA MASMORRA";
        aviso_t = 2;
        if (n & TC_B) {
            modo = MODO_MUNDO;
            wpx = (entrada_x << 16) + 32768;
            wpy = ((entrada_y + 3) << 16) + 32768;
            wang = 64;
            jan_valida = 0;
            janela_atualizar(wpx >> 16, wpy >> 16);
            wcamh = altura_camera();
        }
    }
}

/* -------------------------------------------------------------- tela e HUD */
static void hud(void) {
    retangulo(2, 2, 32, 3, COR_CINZA_ESCURO);
    retangulo(2, 2, (32 * vida) / (vida_max ? vida_max : 1), 3, COR_VERMELHO);
    retangulo(2, 7, 32, 3, COR_CINZA_ESCURO);
    retangulo(2, 7, (32 * folego) / (folego_max ? folego_max : 1), 3, COR_VERDE);
    texto_num(2, 70, "OURO ", ouro, COR_AMARELO);
    texto_num(2, 12, "POÇ ", pocoes, COR_BRANCO);
    if (modo == MODO_MUNDO) {
        static const char *pontos[8] = {"L", "SL", "S", "SO", "O", "NO", "N", "NL"};
        texto(LARG - 14, 2, pontos[((wang + 16) >> 5) & 7], COR_BRANCO);
    }
    if (dano_flash) {
        dano_flash--;
        retangulo(0, 0, LARG, 2, COR_VERMELHO);
        retangulo(0, ALT_T - 2, LARG, 2, COR_VERMELHO);
        retangulo(0, 0, 2, ALT_T, COR_VERMELHO);
        retangulo(LARG - 2, 0, 2, ALT_T, COR_VERMELHO);
    }
    if (aviso_t > 0) {
        aviso_t--;
        retangulo(0, 58, LARG, 10, COR_PAINEL);
        texto_centro(59, aviso_txt, COR_AMARELO);
    }
}

static void redesenhar_cena(void) {
    if (modo == MODO_MASMORRA) {
        desenhar_masmorra();
        desenhar_inimigos();
        desenhar_arma();
    } else {
        desenhar_relevo(wpx, wpy, wang, wcamh);
    }
    hud();
}

static void atualizar_mundo(u16 t, u16 n) {
    s32 c = COS(wang), s = SEN(wang), nx, ny;
    if (t & TC_ESQ) wang -= 3;
    if (t & TC_DIR) wang += 3;
    wang &= 255;
    if (t & (TC_CIMA | TC_BAIXO)) {
        int sg = (t & TC_CIMA) ? 1 : -1;
        nx = wpx + sg * c * VEL_ANDAR;
        ny = wpy + sg * s * VEL_ANDAR;
        if (!bloqueado_mundo(nx, wpy)) wpx = nx;
        if (!bloqueado_mundo(wpx, ny)) wpy = ny;
    }
    if (t & (TC_L | TC_R)) {
        int sg = (t & TC_R) ? 1 : -1;
        nx = wpx + sg * (-s) * VEL_ANDAR;
        ny = wpy + sg * c * VEL_ANDAR;
        if (!bloqueado_mundo(nx, wpy)) wpx = nx;
        if (!bloqueado_mundo(wpx, ny)) wpy = ny;
    }
    s32 pcx = wpx >> 16, pcy = wpy >> 16;
    janela_atualizar(pcx, pcy);
    s32 alvo = altura_camera();
    wcamh += (alvo - wcamh) >> 2;
    if (wcamh < alvo - 2 * 256) wcamh = alvo - 2 * 256;

    Estrutura *e = estrutura_bloco(pcx >> 5, pcy >> 5);
    if (e->tipo && ABS(pcx - e->cx) <= 3 && ABS(pcy - e->cy) <= 3) {
        if (e->tipo == 1) {
            aviso_txt = "A: LOJA E DESCANSO";
            aviso_t = 2;
            if (n & TC_A) { estado = ST_LOJA; loja_sel = 0; }
        } else {
            aviso_txt = "A: ENTRAR NA MASMORRA";
            aviso_t = 2;
            if (n & TC_A) {
                entrada_x = e->cx;
                entrada_y = e->cy;
                gerar_masmorra(hash2(e->cx, e->cy));
                modo = MODO_MASMORRA;
            }
        }
    }
}

/* ------------------------------------------------------------------ menus */
static void lista_opcoes(int x, int y, const char *const *op, int qt, int sel) {
    for (int i = 0; i < qt; i++) {
        if (i == sel) texto(x, y + i * 10, ">", COR_AMARELO);
        texto(x + 8, y + i * 10, op[i], i == sel ? COR_AMARELO : COR_BRANCO);
    }
}

static int mover_sel(int sel, int qt, u16 n) {
    if (n & TC_CIMA) sel = (sel + qt - 1) % qt;
    if (n & TC_BAIXO) sel = (sel + 1) % qt;
    return sel;
}

static void pagina_personagem(void) {
    char vb[32], t1[12], t2[12], mb[32], t3[12];
    retangulo(0, 0, LARG, ALT_T, COR_PAINEL);
    caixa(2, 2, LARG - 4, ALT_T - 4);
    texto_centro(5, "PERSONAGEM", COR_AMARELO);
    texto_num(8, 16, "NÍVEL ", nivel, COR_BRANCO);
    numero_str(vida, t1);
    numero_str(vida_max, t2);
    strcpy(vb, "VIDA ");
    strcat(vb, t1);
    strcat(vb, "/");
    strcat(vb, t2);
    texto(8, 25, vb, COR_VERMELHO);
    texto_num(8, 34, "FÔLEGO ", folego, COR_VERDE);
    texto_num(8, 43, "OURO ", ouro, COR_AMARELO);
    texto_num(8, 52, "ESPADA +", espada, COR_BRANCO);
    texto_num(8 + 6 * 11, 52, "POÇ ", pocoes, COR_BRANCO);
    numero_str(mortos, t3);
    strcpy(mb, "INIMIGOS ");
    strcat(mb, t3);
    strcat(mb, "/30");
    texto(8, 61, mb, COR_BRANCO);
}

static void pagina_ajuda(void) {
    static const char *const linhas[8] = {
        "SETAS: ANDAR/GIRAR", "L/R: LADO A LADO", "A: AÇÃO / ATACAR", "CIMA+A: ESTOCADA",
        "LADO+A: TALHO", "SELECT: POÇÃO", "B: SAIR (MASMORRA)", "START: MENU"};
    retangulo(0, 0, LARG, ALT_T, COR_PAINEL);
    caixa(2, 2, LARG - 4, ALT_T - 4);
    texto_centro(3, "COMO JOGAR", COR_AMARELO);
    for (int i = 0; i < 8; i++) texto(7, 12 + i * 8, linhas[i], COR_BRANCO);
}

static void desenhar_mapa(void) {
    retangulo(0, 0, LARG, ALT_T, COR_PAINEL);
    s32 pcx = wpx >> 16, pcy = wpy >> 16;
    s32 x0 = pcx - LARG, y0 = pcy - ALT_T;
    for (int y = 0; y < ALT_T; y++)
        for (int x = 0; x < LARG; x++) {
            int a = altura_bruta(x0 + x * 2, y0 + y * 2), r;
            if (a < NIVEL_AGUA) r = 0;
            else if (a < NIVEL_AGUA + 3) r = 1;
            else if (a < LIM_ROCHA) r = 3;
            else if (a < LIM_NEVE) r = 5;
            else r = 7;
            fb[y][x] = (u8)COR_RAMPA(r, 1);
        }
    for (s32 by = (y0 >> 5) - 1; by <= ((y0 + 2 * ALT_T) >> 5) + 1; by++)
        for (s32 bx = (x0 >> 5) - 1; bx <= ((x0 + 2 * LARG) >> 5) + 1; bx++) {
            Estrutura *e = estrutura_bloco(bx, by);
            if (!e->tipo) continue;
            int mx = (e->cx - x0) / 2, my = (e->cy - y0) / 2;
            retangulo(mx - 1, my - 1, 3, 3, COR_BRANCO);
            retangulo(mx, my, 1, 1, e->tipo == 1 ? COR_VERMELHO : COR_PAINEL);
        }
    retangulo(LARG / 2 - 2, ALT_T / 2, 5, 1, COR_AMARELO);
    retangulo(LARG / 2, ALT_T / 2 - 2, 1, 5, COR_AMARELO);
    retangulo(0, 0, LARG, 10, COR_PAINEL);
    texto(2, 1, "MAPA  B: VOLTAR", COR_AMARELO);
}

/* ------------------------------------------------------------- passo do jogo */
static const char *const op_titulo[2] = {"NOVA AVENTURA", "COMO JOGAR"};
static const char *const op_titulo_c[3] = {"CONTINUAR", "NOVA AVENTURA", "COMO JOGAR"};
static const char *const op_menu[5] = {"VOLTAR", "PERSONAGEM", "MAPA", "SALVAR", "COMO JOGAR"};
static char loja_txt[4][28];

static void montar_loja(void) {
    char num[12];
    strcpy(loja_txt[0], "DESCANSAR");
    strcpy(loja_txt[1], "POÇÃO ");
    numero_str(CUSTO_POCAO, num);
    strcat(loja_txt[1], num);
    if (espada >= ESPADA_MAX) {
        strcpy(loja_txt[2], "ESPADA MÁXIMA");
    } else {
        strcpy(loja_txt[2], "AFIAR ESPADA ");
        numero_str(25 * (espada + 1), num);
        strcat(loja_txt[2], num);
    }
    strcpy(loja_txt[3], "SAIR");
}
static int ang_titulo;

static void passo(u16 t, u16 n) {
    switch (estado) {
    case ST_TITULO:
        ang_titulo = (ang_titulo + 1) & 255;
        janela_atualizar(wpx >> 16, wpy >> 16);
        desenhar_relevo(wpx, wpy, ang_titulo, wcamh);
        caixa(8, 4, 104, 24);
        texto_centro(8, "THE ELDER SCROLLS", COR_AMARELO);
        texto_centro(18, "DAGGERFALL GBA", COR_BRANCO);
        {
            int qt = tem_save ? 3 : 2;
            caixa(14, tem_save ? 40 : 50, 92, tem_save ? 36 : 26);
            lista_opcoes(20, tem_save ? 44 : 54, tem_save ? op_titulo_c : op_titulo, qt, menu_sel);
            menu_sel = mover_sel(menu_sel, qt, n);
            if (n & (TC_A | TC_START)) {
                int acao = tem_save ? menu_sel : menu_sel + 1;
                if (acao == 0) { carregar_jogo(); estado = ST_JOGO; }
                else if (acao == 1) { jogo_novo(); estado = ST_JOGO; }
                else { pagina_origem = ST_TITULO; estado = ST_AJUDA; }
            }
        }
        break;
    case ST_JOGO:
        if (n & TC_START) {
            estado = ST_MENU;
            menu_sel = 0;
            menu_redesenhar = 0;
            break;
        }
        if (n & TC_SELECT) beber_pocao();
        if (modo == MODO_MUNDO) atualizar_mundo(t, n);
        else atualizar_masmorra(t, n);
        if (estado != ST_JOGO) break;
        redesenhar_cena();
        break;
    case ST_MENU:
        if (menu_redesenhar) { redesenhar_cena(); menu_redesenhar = 0; }
        caixa(16, 6, 88, 58);
        lista_opcoes(22, 11, op_menu, 5, menu_sel);
        menu_sel = mover_sel(menu_sel, 5, n);
        if (n & (TC_B | TC_START)) estado = ST_JOGO;
        else if (n & TC_A) {
            if (menu_sel == 0) estado = ST_JOGO;
            else if (menu_sel == 1) estado = ST_PERSONAGEM;
            else if (menu_sel == 2) { estado = ST_MAPA; mapa_pronto = 0; }
            else if (menu_sel == 3) {
                if (modo == MODO_MUNDO) { salvar_jogo(); aviso("JOGO SALVO!"); }
                else aviso("SAIA DA MASMORRA PRA SALVAR");
                estado = ST_JOGO;
            }
            else { pagina_origem = ST_MENU; estado = ST_AJUDA; }
        }
        break;
    case ST_PERSONAGEM:
        pagina_personagem();
        if (n & (TC_B | TC_A)) { estado = ST_MENU; menu_redesenhar = 1; }
        break;
    case ST_MAPA:
        if (!mapa_pronto) { desenhar_mapa(); mapa_pronto = 1; }
        if (n & (TC_B | TC_A)) { estado = ST_MENU; menu_redesenhar = 1; }
        break;
    case ST_AJUDA:
        pagina_ajuda();
        if (n & (TC_B | TC_A)) {
            estado = pagina_origem;
            menu_redesenhar = 1;
        }
        break;
    case ST_LOJA:
        montar_loja();
        caixa(6, 6, 108, 66);
        {
            char r1[16], r2[16];
            numero_str(ouro, r1);
            numero_str(pocoes, r2);
            texto(12, 9, "OURO", COR_AMARELO);
            texto(12 + 30, 9, r1, COR_BRANCO);
            texto(12 + 60, 9, "POÇ", COR_BRANCO);
            texto(12 + 84, 9, r2, COR_BRANCO);
        }
        {
            const char *ops[4] = {loja_txt[0], loja_txt[1], loja_txt[2], loja_txt[3]};
            lista_opcoes(12, 21, ops, 4, loja_sel);
        }
        loja_sel = mover_sel(loja_sel, 4, n);
        if (n & TC_B) estado = ST_JOGO;
        else if (n & TC_A) {
            if (loja_sel == 0) {
                vida = vida_max; folego = folego_max;
                aviso("VOCÊ DESCANSOU.");
                estado = ST_JOGO;
            } else if (loja_sel == 1) {
                if (ouro >= CUSTO_POCAO && pocoes < 9) { ouro -= CUSTO_POCAO; pocoes++; }
                else aviso(pocoes >= 9 ? "BOLSA CHEIA!" : "OURO INSUFICIENTE!");
            } else if (loja_sel == 2) {
                int custo = 25 * (espada + 1);
                if (espada >= ESPADA_MAX) aviso("ESPADA JÁ É MÁXIMA!");
                else if (ouro >= custo) { ouro -= custo; espada++; aviso("ESPADA AFIADA!"); }
                else aviso("OURO INSUFICIENTE!");
            } else estado = ST_JOGO;
        }
        if (aviso_t > 0 && estado == ST_LOJA) {
            aviso_t--;
            retangulo(8, 62, 104, 8, COR_PAINEL);
            texto_centro(62, aviso_txt, COR_AMARELO);
        }
        break;
    case ST_VITORIA:
        caixa(8, 14, 104, 50);
        texto_centro(22, "VITÓRIA!", COR_AMARELO);
        texto_centro(34, "MASMORRAS LIVRES", COR_BRANCO);
        texto_centro(42, "DOS MONSTROS!", COR_BRANCO);
        texto_centro(52, "A: CONTINUAR", COR_VERDE);
        if (n & TC_A) estado = ST_JOGO;
        break;
    case ST_MORTE:
        caixa(18, 22, 84, 32);
        texto_centro(28, "VOCÊ MORREU", COR_VERMELHO);
        texto_centro(40, "A: CONTINUAR", COR_BRANCO);
        if (n & TC_A) {
            ouro /= 2;
            vida = vida_max;
            folego = folego_max;
            modo = MODO_MUNDO;
            ir_para_aldeia_inicial();
            estado = ST_JOGO;
        }
        break;
    }
}

/* ---------------------------------------------------------- inicialização */
static void iniciar_tabelas(void) {
    int i;
    for (i = 0; i < 256; i++) sen_t[i] = (s16)lround(256.0 * sin(i * 6.283185307179586 / 256.0));
    for (i = 0; i < LARG; i++) camx_t[i] = (s16)(((2 * i - (LARG - 1)) * 256) / (LARG - 1));
    for (i = 1; i <= PROF; i++) {
        nevoa_t[i] = (u8)((i * 11) / PROF);
        mul_t[i] = (ESCALA_V * 256) / i;
    }
}

#ifndef TESTE_PC
FUNC_IWRAM static void mostrar_quadro(void) {
    volatile u32 *dest =
        (volatile u32 *)(MEM_VRAM + ((REG_DISPCNT & 0x10) ? 0 : 0x5000));
    for (int y = 0; y < ALT_T; y++) {
        const u32 *src = (const u32 *)fb[y];
        volatile u32 *d0 = dest + y * 120;
        volatile u32 *d1 = d0 + 60;
        for (int x = 0; x < 30; x++) {
            u32 q = src[x];
            u32 p0 = q & 0xFF, p1 = (q >> 8) & 0xFF, p2 = (q >> 16) & 0xFF, p3 = q >> 24;
            u32 a = p0 | (p0 << 8) | (p1 << 16) | (p1 << 24);
            u32 b = p2 | (p2 << 8) | (p3 << 16) | (p3 << 24);
            d0[2 * x] = a; d0[2 * x + 1] = b;
            d1[2 * x] = a; d1[2 * x + 1] = b;
        }
    }
    while (REG_VCONT >= 160) {}
    while (REG_VCONT < 160) {}
    REG_DISPCNT ^= 0x10;
}

int main(void) {
    iniciar_tabelas();
    for (int i = 0; i < 256; i++) MEM_PALETA[i] = PALETA[i];
    REG_DISPCNT = 4 | 0x0400;      /* modo 4, fundo 2 ligado */
    tem_save = carregar_ok();
    estado = ST_TITULO;
    jogo_novo();
    u16 antes = 0;
    for (;;) {
        u16 t = (u16)(~REG_TECLAS) & 0x3FF;
        u16 n = t & ~antes;
        antes = t;
        passo(t, n);
        mostrar_quadro();
    }
    return 0;
}

#else
/* --------------------------------------------- teste no computador (sem GBA) */
static void salvar(const char *nome) {
    FILE *f = fopen(nome, "wb");
    fwrite(fb, 1, sizeof fb, f);
    fclose(f);
}

static void mover_perto(int tipo_estr, int dist, int angulo, s32 *ox, s32 *oy) {
    for (int r = 0; r <= 8; r++)
        for (int by = -r; by <= r; by++)
            for (int bx = -r; bx <= r; bx++) {
                if (ABS(bx) != r && ABS(by) != r) continue;
                Estrutura *e = estrutura_bloco(bx, by);
                if (e->tipo == tipo_estr) {
                    *ox = e->cx; *oy = e->cy;
                    s32 c = COS(angulo), s = SEN(angulo);
                    wpx = (e->cx << 16) + 32768 - c * 256 * dist;
                    wpy = (e->cy << 16) + 32768 - s * 256 * dist;
                    wang = angulo;
                    jan_valida = 0;
                    janela_atualizar(wpx >> 16, wpy >> 16);
                    wcamh = altura_camera();
                    return;
                }
            }
}

int main(void) {
    char nome[64];
    s32 ax = 0, ay = 0;
    iniciar_tabelas();
    estado = ST_TITULO;
    jogo_novo();
    passo(0, 0);
    salvar("q_titulo.raw");
    estado = ST_JOGO;
    for (int a = 0; a < 4; a++) {
        wang = 192 + a * 64;
        passo(0, 0);
        snprintf(nome, sizeof nome, "q_mundo%d.raw", a);
        salvar(nome);
    }
    wang = 192;
    for (int d = 22; d >= 4; d -= 6) {
        mover_perto(1, d, 192 + 0, &ax, &ay);
        wang = 64 * 0 + 0;
        /* olha na direção do centro da aldeia */
        {
            s32 dx = (ax << 16) + 32768 - wpx, dy = (ay << 16) + 32768 - wpy;
            wang = (int)lround(atan2((double)dy, (double)dx) * 256.0 / 6.283185307179586) & 255;
        }
        passo(0, 0);
        snprintf(nome, sizeof nome, "q_aldeia_%d.raw", d);
        salvar(nome);
    }
    for (int d = 20; d >= 4; d -= 8) {
        mover_perto(2, d, 0, &ax, &ay);
        {
            s32 dx = (ax << 16) + 32768 - wpx, dy = (ay << 16) + 32768 - wpy;
            wang = (int)lround(atan2((double)dy, (double)dx) * 256.0 / 6.283185307179586) & 255;
        }
        passo(0, 0);
        snprintf(nome, sizeof nome, "q_masm_%d.raw", d);
        salvar(nome);
    }
    /* masmorra por dentro */
    entrada_x = ax; entrada_y = ay;
    gerar_masmorra(hash2(ax, ay));
    modo = MODO_MASMORRA;
    passo(0, 0);
    salvar("q_dentro0.raw");
    for (int i = 0; i < 20; i++) passo(TC_CIMA, 0);
    salvar("q_dentro1.raw");
    /* coloca um inimigo na frente */
    ini[0].x = mpx + COS(mang); ini[0].y = mpy + SEN(mang);
    ini[0].vivo = 1; ini[0].vida = 14;
    passo(0, 0);
    salvar("q_inimigo0.raw");
    passo(0, TC_A);
    for (int i = 0; i < 6; i++) passo(0, 0);
    salvar("q_golpe.raw");
    for (int i = 0; i < 12; i++) passo(0, 0);
    printf("inimigo0 vida=%d vivo=%d folego=%d\n", ini[0].vida, ini[0].vivo, folego);
    estado = ST_MENU; menu_redesenhar = 0;
    passo(0, 0);
    salvar("q_menu.raw");
    estado = ST_PERSONAGEM; passo(0, 0); salvar("q_pers.raw");
    estado = ST_AJUDA; passo(0, 0); salvar("q_ajuda.raw");
    modo = MODO_MUNDO; ir_para_aldeia_inicial();
    estado = ST_MAPA; mapa_pronto = 0; passo(0, 0); salvar("q_mapa.raw");
    estado = ST_MORTE; passo(0, 0); salvar("q_morte.raw");
    /* loja, salvamento, título com "continuar" e vitória */
    modo = MODO_MUNDO; ir_para_aldeia_inicial(); ouro = 60; pocoes = 1; estado = ST_JOGO;
    mover_perto(1, 2, 0, &ax, &ay);
    passo(0, 0);
    passo(0, TC_A);
    passo(0, 0);
    printf("estado da loja=%d (esperado %d)\n", estado, ST_LOJA);
    passo(0, TC_BAIXO);
    passo(0, TC_A);
    printf("compra de poção: ouro=%d pocoes=%d\n", ouro, pocoes);
    passo(0, TC_BAIXO);
    passo(0, TC_A);
    printf("afiar espada: ouro=%d espada=%d\n", ouro, espada);
    salvar("q_loja.raw");
    estado = ST_JOGO; salvar_jogo();
    { int o = ouro, e = espada, p = pocoes; ouro = 0; espada = 0; pocoes = 0; carregar_jogo();
      printf("salvar/carregar: %s\n", (o == ouro && e == espada && p == pocoes) ? "ok" : "ERRO"); }
    tem_save = 1; estado = ST_TITULO; menu_sel = 0; passo(0, 0); salvar("q_titulo_save.raw");
    estado = ST_VITORIA; passo(0, 0); salvar("q_vitoria.raw");
    printf("ok\n");
    return 0;
}
#endif
