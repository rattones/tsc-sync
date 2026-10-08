/*
 * i18n.c - deteccao de idioma e catalogos do tsc-sync. Ver i18n.h.
 */
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "i18n.h"

/* Ingles: completo por construcao (a lista de chaves sai do proprio en.h). */
static const char *const en[M_COUNT] = {
#define MSG(key, text) [M_##key] = text,
#include "i18n/en.h"
#undef MSG
};

/* Portugues: o que nao estiver aqui fica NULL e cai para o ingles. */
static const char *const pt[M_COUNT] = {
#include "i18n/pt.h"
};

static const char *const *active = en;
static const char *code = "en";

/* Primeira variavel de locale definida, na precedencia POSIX (LC_ALL manda,
 * LC_MESSAGES rege o texto, LANG e o padrao), com TSC_SYNC_LANG acima de todas
 * - e o que os testes fixam. pt_BR.UTF-8 -> "pt"; C, POSIX e vazio -> ingles. */
void i18n_init(void)
{
    const char *vars[] = { "TSC_SYNC_LANG", "LC_ALL", "LC_MESSAGES", "LANG" };
    const char *raw = "";
    for (size_t i = 0; i < sizeof vars / sizeof vars[0]; i++) {
        const char *v = getenv(vars[i]);
        if (v && *v) { raw = v; break; }
    }
    char lang[8] = "";
    size_t j = 0;
    for (const char *p = raw; *p && *p != '_' && *p != '.' && *p != '@' && j < sizeof lang - 1; p++)
        lang[j++] = (char)tolower((unsigned char)*p);
    lang[j] = '\0';
    if (!strcmp(lang, "pt")) { active = pt; code = "pt"; }
}

const char *msg(msg_id id)
{
    if (id >= M_COUNT) return "<?>";
    const char *s = active[id];
    return s ? s : en[id];
}

const char *i18n_lang(void) { return code; }
