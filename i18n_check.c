/*
 * i18n_check.c - confere os catalogos (make check).
 *
 * As mensagens viram formato de printf em tempo de execucao, entao o compilador
 * nao confere os argumentos. Este programa confere, chave por chave, que cada
 * traducao usa exatamente os mesmos marcadores (%d, %s, %+.3f...) do ingles e na
 * mesma ordem: uma troca de %d por %s seria comportamento indefinido. Tambem
 * lista as chaves que faltam traduzir (essas caem para o ingles, sem erro).
 */
#include <stdio.h>
#include "i18n.c"

static const char *const keys[M_COUNT] = {
#define MSG(key, text) [M_##key] = #key,
#include "i18n/en.h"
#undef MSG
};

/* Copia so os marcadores de conversao de s (sem o texto), em ordem. */
static void specs(const char *s, char *out, size_t cap)
{
    size_t n = 0;
    for (; *s && n + 1 < cap; s++) {
        if (*s != '%') continue;
        if (s[1] == '%') { s++; continue; }
        do { if (n + 1 < cap) out[n++] = *s; } while (*s && !strchr("diouxXeEfFgGaAcspn", *s++) && *s);
        s--;
        if (n + 1 < cap) out[n++] = ' ';
    }
    out[n] = '\0';
}

int main(void)
{
    struct { const char *name; const char *const *cat; } langs[] = { { "pt", pt } };
    int errors = 0;
    for (size_t l = 0; l < sizeof langs / sizeof langs[0]; l++) {
        int missing = 0;
        for (int k = 0; k < M_COUNT; k++) {
            if (!langs[l].cat[k]) { printf("%s: falta traduzir %s\n", langs[l].name, keys[k]); missing++; continue; }
            char a[128], b[128];
            specs(en[k], a, sizeof a);
            specs(langs[l].cat[k], b, sizeof b);
            if (strcmp(a, b)) {
                printf("%s: %s tem marcadores diferentes do ingles: [%s] vs [%s]\n", langs[l].name, keys[k], b, a);
                errors++;
            }
        }
        printf("%s: %d chaves, %d sem traducao, %d com marcadores errados\n",
               langs[l].name, M_COUNT, missing, errors);
    }
    return errors ? 1 : 0;
}
