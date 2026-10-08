/*
 * tsc-sync - mede e (opcionalmente) corrige TSCs dessincronizados entre CPUs.
 *
 * Contexto: em alguns notebooks AMD o firmware liga a CPU 0 com o TSC segundos
 * atras das outras. O kernel detecta ("Measured N cycles TSC warp between
 * CPUs"), mas sem TSC_ADJUST (AMD) nao consegue corrigir: so passa a usar o HPET
 * como relogio. Programas que leem o TSC direto (RDTSC) continuam vendo o salto,
 * e um spin-wait cronometrado pelo TSC que cai na CPU 0 gira pela diferenca
 * inteira.
 *
 * Uso:
 *   tsc-sync                 mede (padrao) e mostra a diferenca de cada CPU
 *   tsc-sync measure         o mesmo; sai 0 se sincronizado, 3 se nao
 *   tsc-sync sync --dry-run  mostra o que gravaria, sem gravar (root)
 *   tsc-sync sync            grava o TSC das CPUs fora do grupo (root)
 *   opcoes: --tol-us N       tolerancia para "sincronizado" (padrao 50 us)
 *           --quiet          so erros e o resultado final
 *
 * Como mede (sem depender da frequencia do TSC): para comparar a CPU k com a
 * de referencia R, le o TSC de R, migra para k, le o TSC de k, volta para R e
 * le de novo. Cada leitura vem entre duas leituras de CLOCK_MONOTONIC_RAW (um
 * relogio unico para todas as CPUs), e fica a de janela mais estreita entre
 * varias tentativas. Interpolando as duas leituras de R no instante da leitura
 * de k, a diferenca sai direto em ciclos. Repete varias vezes e usa a mediana.
 *
 * Como corrige: pinado na CPU alvo, le o proprio TSC e grava (MSR 0x10, via
 * /dev/cpu/N/msr) esse valor mais a diferenca e mais o custo estimado da
 * propria gravacao. Depois mede de novo e refina, ate ficar dentro da
 * tolerancia (no maximo 6 passadas). So mexe nas CPUs que discordam da
 * maioria; a maioria vira a referencia.
 *
 * Compilar: gcc -O2 -Wall -o tsc-sync tsc-sync.c
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <x86intrin.h>
#include "i18n.h"

#define MSR_IA32_TSC 0x10
#define ROUNDS       15     /* medicoes R-k-R por CPU; usa a mediana */
#define TRIES        64     /* tentativas por leitura; fica a janela mais estreita */
#define MAX_PASSES   6      /* refinamentos da gravacao */

static int quiet;

static double now_raw(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return ts.tv_sec + ts.tv_nsec / 1e9;
}

static int pin(int cpu)
{
    cpu_set_t s;
    CPU_ZERO(&s);
    CPU_SET(cpu, &s);
    return sched_setaffinity(0, sizeof s, &s);
}

/* Uma leitura do TSC desta CPU com o instante no relogio global; *win = largura
 * da janela (incerteza). Fica a melhor de TRIES tentativas. */
static void sample(uint64_t *tsc, double *t, double *win)
{
    double best = 1e9;
    for (int i = 0; i < TRIES; i++) {
        unsigned aux;
        double a = now_raw();
        uint64_t x = __rdtscp(&aux);
        double b = now_raw();
        if (b - a < best) { best = b - a; *tsc = x; *t = (a + b) / 2; }
    }
    *win = best;
}

/* pin com o erro ja relatado (traduzido), para quem chama so desistir. */
static int pin_report(int cpu)
{
    if (pin(cpu) == 0) return 0;
    fprintf(stderr, msg(M_err_affinity), cpu, strerror(errno));
    return -1;
}

static int cmp_i64(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

/* Diferenca em ciclos: TSC(ref) - TSC(k) no mesmo instante. Positivo = a CPU k
 * esta atras da referencia. *unc_us = maior janela usada (incerteza). */
static int delta_cycles(int ref, int k, int64_t *out, double *unc_us)
{
    int64_t d[ROUNDS];
    double worst = 0;
    for (int r = 0; r < ROUNDS; r++) {
        uint64_t ta, tb, tc; double sa, sb, sc, wa, wb, wc;
        if (pin_report(ref)) return -1;
        sample(&ta, &sa, &wa);
        if (pin_report(k)) return -1;
        sample(&tb, &sb, &wb);
        if (pin_report(ref)) return -1;
        sample(&tc, &sc, &wc);
        /* TSC da referencia interpolado no instante da leitura de k */
        double f = (sc > sa) ? (sb - sa) / (sc - sa) : 0.5;
        double ref_at_b = (double)ta + f * (double)(tc - ta);
        d[r] = (int64_t)(ref_at_b - (double)tb);
        double w = wa > wb ? wa : wb; w = w > wc ? w : wc;
        if (w > worst) worst = w;
    }
    qsort(d, ROUNDS, sizeof d[0], cmp_i64);
    *out = d[ROUNDS / 2];
    *unc_us = worst * 1e6;
    return 0;
}

/* Ciclos por segundo, so para converter para unidades legiveis. */
static double tsc_hz(int cpu)
{
    uint64_t t0, t1; double s0, s1, w;
    pin(cpu);
    sample(&t0, &s0, &w);
    struct timespec ts = { 0, 200 * 1000 * 1000 };
    nanosleep(&ts, NULL);
    sample(&t1, &s1, &w);
    return (double)(t1 - t0) / (s1 - s0);
}

static int msr_rw(int cpu, int write, uint64_t *val)
{
    char path[64];
    snprintf(path, sizeof path, "/dev/cpu/%d/msr", cpu);
    int fd = open(path, write ? O_WRONLY : O_RDONLY);
    if (fd < 0) return -1;
    ssize_t n = write ? pwrite(fd, val, 8, MSR_IA32_TSC) : pread(fd, val, 8, MSR_IA32_TSC);
    close(fd);
    return n == 8 ? 0 : -1;
}

/* Grava TSC = TSC atual + delta na CPU k, pinado nela. adj = ciclos que se
 * passam entre o RDTSC e a gravacao efetiva (estimado pelo custo de um pread). */
static int write_tsc(int k, int64_t delta, int64_t adj)
{
    char path[64];
    snprintf(path, sizeof path, "/dev/cpu/%d/msr", k);
    int fd = open(path, O_WRONLY);
    if (fd < 0) return -1;
    if (pin(k)) { close(fd); return -1; }
    unsigned aux;
    uint64_t v = __rdtscp(&aux) + (uint64_t)delta + (uint64_t)adj;
    ssize_t n = pwrite(fd, &v, 8, MSR_IA32_TSC);
    close(fd);
    return n == 8 ? 0 : -1;
}

/* Custo, em ciclos, de um acesso ao MSR feito na propria CPU (mediana). */
static int64_t msr_cost(int k)
{
    int64_t c[15];
    uint64_t v;
    if (pin(k)) return 0;
    for (int i = 0; i < 15; i++) {
        unsigned aux;
        uint64_t a = __rdtscp(&aux);
        if (msr_rw(k, 0, &v)) return 0;
        uint64_t b = __rdtscp(&aux);
        c[i] = (int64_t)(b - a);
    }
    qsort(c, 15, sizeof c[0], cmp_i64);
    return c[7];
}

static void usage(void)
{
    fputs(msg(M_usage), stderr);
}

int main(int argc, char **argv)
{
    i18n_init();
    int do_sync = 0, dry = 0;
    double tol_us = 50;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "measure")) do_sync = 0;
        else if (!strcmp(argv[i], "sync")) do_sync = 1;
        else if (!strcmp(argv[i], "--dry-run")) dry = 1;
        else if (!strcmp(argv[i], "--quiet")) quiet = 1;
        else if (!strcmp(argv[i], "--tol-us") && i + 1 < argc) tol_us = atof(argv[++i]);
        else if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) { fputs(msg(M_usage), stdout); return 0; }
        else { usage(); return 2; }
    }

    /* Linha a linha: com o servico, a saida vai para o journal na ordem certa. */
    setvbuf(stdout, NULL, _IOLBF, 0);
    if (do_sync && !dry && geteuid() != 0) {
        fputs(msg(M_err_need_root), stderr);
        return 1;
    }
    int n = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n < 2) { fputs(msg(M_measure_single_cpu), stdout); return 0; }
    double hz = tsc_hz(n - 1);
    int64_t tol = (int64_t)(tol_us * 1e-6 * hz);

    /* 1) Diferenca de cada CPU em relacao a ultima; a maioria define o grupo. */
    int64_t *o = calloc(n, sizeof *o), *sorted = calloc(n, sizeof *sorted);
    double unc, worst_unc = 0;
    int last = n - 1;
    for (int k = 0; k < n; k++) {
        if (k == last) { o[k] = 0; continue; }
        if (delta_cycles(last, k, &o[k], &unc)) return 1;
        /* o[k] = TSC(last) - TSC(k): quanto k esta atras da ultima */
        if (unc > worst_unc) worst_unc = unc;
    }
    memcpy(sorted, o, n * sizeof *o);
    qsort(sorted, n, sizeof *sorted, cmp_i64);
    int64_t med = sorted[n / 2];
    int ref = -1, out_of_sync = 0;
    for (int k = 0; k < n; k++)
        if (llabs(o[k] - med) <= tol) { if (ref < 0) ref = k; } else out_of_sync++;

    if (!quiet || out_of_sync) {
        printf(msg(M_measure_header), hz / 1e6, tol_us, worst_unc);
        printf(msg(M_measure_reference), ref);
        for (int k = 0; k < n; k++) {
            int64_t d = o[k] - med;   /* >0: atras do grupo */
            if (llabs(d) <= tol)
                printf("  CPU %2d  %+10.1f us   %s\n", k, -(double)d / hz * 1e6, msg(M_measure_ok));
            else
                printf("  CPU %2d  %+10.6f s    %s\n", k, -(double)d / hz,
                       msg(d > 0 ? M_measure_behind : M_measure_ahead));
        }
    }
    if (!out_of_sync) { fputs(msg(M_result_synced), stdout); return 0; }
    if (!do_sync) { printf(msg(M_result_out_of_sync), out_of_sync); return 3; }

    /* 2) Sincronizar so as CPUs fora do grupo, contra a referencia. */
    int failed = 0;
    for (int k = 0; k < n; k++) {
        if (llabs(o[k] - med) <= tol) continue;
        int64_t d;
        if (delta_cycles(ref, k, &d, &unc)) return 1;
        if (dry) {
            printf(msg(M_sync_would_write), k, (long long)d, d / hz);
            continue;
        }
        int64_t adj = msr_cost(k);
        int pass;
        for (pass = 1; pass <= MAX_PASSES && llabs(d) > tol; pass++) {
            if (write_tsc(k, d, adj)) {
                fprintf(stderr, msg(M_sync_write_failed), k, strerror(errno));
                return 1;
            }
            if (delta_cycles(ref, k, &d, &unc)) return 1;
            if (!quiet)
                printf(msg(M_sync_pass), k, pass, d / hz * 1e6);
        }
        if (llabs(d) > tol) {
            fprintf(stderr, msg(M_sync_no_converge), k, d / hz * 1e6);
            failed++;
        } else {
            printf(msg(M_sync_done), k, d / hz * 1e6);
        }
    }
    if (dry) return 3;
    return failed ? 4 : 0;
}
