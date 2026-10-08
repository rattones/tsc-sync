/*
 * pt.h - catalogo em portugues. Sobrepoe o ingles (en.h): uma chave que faltar
 * aqui sai em ingles, nunca vazia. Mesmas convencoes do en.h.
 *
 * Cada linha: [M_chave] = "texto",
 */
[M_usage] =
    "uso: tsc-sync [measure | sync [--dry-run]] [--tol-us N] [--quiet]\n"
    "  measure          mede a diferença de TSC de cada CPU (padrão)\n"
    "  sync             grava o TSC das CPUs fora do grupo da maioria (root)\n"
    "  sync --dry-run   mostra o que gravaria, sem gravar\n"
    "  --tol-us N       tolerância para \"sincronizado\", em microssegundos (padrão 50)\n"
    "  --quiet          só erros e o resultado final\n"
    "idioma: TSC_SYNC_LANG, LC_ALL, LC_MESSAGES ou LANG (en, pt)\n",
[M_err_need_root]      = "sync precisa de root (para só ver o que faria: sync --dry-run)\n",
[M_err_affinity]       = "não consegui ir para a CPU %d: %s\n",
[M_measure_single_cpu] = "só uma CPU: nada a comparar\n",
[M_measure_header]     = "TSC a %.3f MHz; tolerância %.0f us; incerteza da medição ~%.1f us\n",
[M_measure_reference]  = "referência: CPU %d (grupo da maioria)\n",
[M_measure_ok]         = "ok",
[M_measure_behind]     = "ATRASADA",
[M_measure_ahead]      = "ADIANTADA",
[M_result_synced]      = "resultado: sincronizado\n",
[M_result_out_of_sync] = "resultado: %d CPU(s) fora de sincronia\n",
[M_sync_would_write]   = "CPU %d: gravaria TSC + %lld ciclos (%+.6f s)\n",
[M_sync_write_failed]  = "CPU %d: falha ao gravar o MSR 0x10 (%s); modprobe msr? lockdown do kernel?\n",
[M_sync_pass]          = "CPU %d: passada %d, resta %+.3f us\n",
[M_sync_no_converge]   = "CPU %d: não convergiu (resta %+.3f us)\n",
[M_sync_done]          = "CPU %d: sincronizada (resta %+.3f us)\n",
