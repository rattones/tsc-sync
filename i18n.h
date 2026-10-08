/*
 * i18n.h - mensagens traduzidas do tsc-sync.
 *
 * Mesmo padrao do monitor: o ingles (i18n/en.h) e o idioma de origem e a base,
 * e cada traducao (i18n/<idioma>.h) se sobrepoe a ele chave a chave. O idioma
 * vem de TSC_SYNC_LANG, LC_ALL, LC_MESSAGES ou LANG, nessa ordem; "C" e "POSIX"
 * valem ingles. Os catalogos sao compilados no binario: um programa que roda
 * como root nao deve ler texto de arquivos em tempo de execucao.
 */
#ifndef TSC_SYNC_I18N_H
#define TSC_SYNC_I18N_H

typedef enum {
#define MSG(key, text) M_##key,
#include "i18n/en.h"
#undef MSG
    M_COUNT
} msg_id;

void i18n_init(void);            /* detecta o idioma; chamar uma vez no inicio */
const char *msg(msg_id id);      /* o texto no idioma detectado (ou em ingles) */
const char *i18n_lang(void);     /* "en" ou "pt" */

#endif
