# make           compila
# make check     confere os catalogos de idioma, mede (sem root) e mostra o que o sync faria
CFLAGS ?= -O2 -Wall -Wextra

tsc-sync: tsc-sync.c i18n.c i18n.h i18n/en.h i18n/pt.h
	$(CC) $(CFLAGS) -o $@ tsc-sync.c i18n.c

i18n_check: i18n_check.c i18n.c i18n.h i18n/en.h i18n/pt.h
	$(CC) $(CFLAGS) -o $@ i18n_check.c

check: tsc-sync i18n_check
	./i18n_check
	./tsc-sync measure; test $$? -le 3
	./tsc-sync sync --dry-run; test $$? -le 3

clean:
	rm -f tsc-sync i18n_check

.PHONY: check clean
