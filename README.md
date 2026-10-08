# tsc-sync

*[English](README.en.md)*

Mede e, se você pedir, corrige o TSC (time-stamp counter) de CPUs que o firmware
liga fora de sincronia. **Estado: experimental, validado num único notebook.**
Instalar não ativa nada.

## O problema

Em alguns notebooks AMD Ryzen, o firmware liga a **CPU 0 com o TSC alguns
segundos atrás** das outras. No notebook estudado (um Ryzen 7 da série 5000,
"Cezanne", já com o BIOS mais recente do fabricante), só a CPU 0 ficava para
trás, e as outras 15 concordavam entre si. A diferença muda a cada boot (2,0 a
20,7 s em 89 boots) e acompanha o tempo que o firmware leva no POST. O kernel
detecta isso no boot:

```
Measured 10821117556 cycles TSC warp between CPUs, turning off TSC clock.
tsc: Marking TSC unstable due to check_tsc_sync_source failed
clocksource: Switched to clocksource hpet
```

Esses Ryzen não têm `TSC_ADJUST`, que é o recurso que o Linux usa para
ressincronizar. Por isso o kernel só passa a usar o HPET como relógio. O relógio
do sistema fica certo, mas **programas que leem o TSC direto (instrução `RDTSC`)
continuam vendo a CPU 0 atrasada**. O Dota 2 cronometra esperas curtas
(`ThreadSpin`) assim: quando uma thread migra para a CPU 0 no meio da espera, o
relógio "volta" alguns segundos e o jogo trava pelo tempo da diferença
(ValveSoftware/Dota-2#3558). O `amd_prefcore=disable` diminuiu a frequência
(~85%), mas não elimina, porque threads ainda caem na CPU 0 às vezes.

## O que o tsc-sync faz

```
tsc-sync                      # mede (padrão, sem root)
sudo tsc-sync sync --dry-run  # mostra o que gravaria
sudo tsc-sync sync            # grava
```

**Medir.** Para comparar a CPU *k* com a referência *R*, lê o TSC de *R*, migra
para *k*, lê o TSC de *k*, volta para *R* e lê de novo. Cada leitura vem entre
duas leituras de `CLOCK_MONOTONIC_RAW` (o HPET, um relógio único para todas as
CPUs), e fica a de janela mais estreita entre 64 tentativas. Interpolando as
duas leituras de *R* no instante da leitura de *k*, a diferença sai **direto em
ciclos, sem depender da frequência do TSC**. Repete 15 vezes e usa a mediana. A
referência é uma CPU do grupo da maioria; só as que discordam dele são
corrigidas.

Exemplo, num boot em que o kernel mediu 10.821.117.556 ciclos:

```
TSC a 3194.000 MHz; tolerância 50 us; incerteza da medição ~1.4 us
referência: CPU 1 (grupo da maioria)
  CPU  0   -3.387952 s    ATRASADA
  CPU  1        +0.0 us   ok
  CPU  2        -0.1 us   ok
  ...
CPU 0: gravaria TSC + 10821117635 ciclos (+3.387953 s)
```

A medição bate com a do kernel com diferença de algumas dezenas de ciclos
(dezenas de nanossegundos), e as 15 CPUs do grupo concordam com menos de 1 µs
entre si.

**Corrigir.** Pinado na CPU atrasada, lê o próprio TSC e grava no registrador
MSR `0x10` (`IA32_TSC`, via `/dev/cpu/N/msr`) esse valor mais a diferença e mais
o custo estimado da própria gravação. Depois mede de novo e refina, até ficar
dentro da tolerância (50 µs por padrão, no máximo 6 passadas). Uma gravação vale
até o próximo boot.

| Saída | Significado |
|---|---|
| 0 | sincronizado (ou sincronizou) |
| 1 | erro: sem root, `/dev/cpu/N/msr` ausente, gravação recusada |
| 2 | uso inválido |
| 3 | fora de sincronia (no `measure` e no `--dry-run`) |
| 4 | gravou, mas alguma CPU não convergiu |

## Resultados no notebook testado

Com o serviço ativo, em dois boots (kernel 7.0):

| Boot | CPU 0 atrasada | Depois do `sync` |
|---|---|---|
| 1 | 3,158645 s (o kernel mediu 3,158 s) | +6,0 µs, numa passada |
| 2 | 3,971967 s | +4,5 µs, numa passada |

Cinco horas depois do primeiro boot, `tsc-sync` ainda dizia "sincronizado". Numa
partida de Dota 2 de 71 minutos nesse estado (com `amd_prefcore=disable`), a GPU
não parou nenhuma vez por 0,5 s ou mais. Antes eram ~58 paradas de 1 s ou mais
por hora, e ~8 por hora só com o `amd_prefcore=disable`. A volta da suspensão
ainda não foi testada.

## Instalar

```bash
sudo ./install.sh              # compila e instala; NÃO ativa
sudo ./install.sh --enable     # instala e ativa no boot e na volta da suspensão
sudo ./install.sh --disable    # desativa, mas mantém instalado (para medir)
sudo ./install.sh --uninstall  # desativa e remove
make check                     # confere os catálogos, mede e mostra o --dry-run, sem root
```

Instala `/usr/local/sbin/tsc-sync` e duas units:

- `tsc-sync.service`: cedo no boot (antes do `sysinit.target`), para que nenhum
  programa do usuário veja o salto;
- `tsc-sync-resume.service`: ao voltar da suspensão ou da hibernação. Se tudo
  estiver sincronizado, só mede e sai.

## O que testar antes de ativar

1. `tsc-sync`: confirmar que só a CPU 0 aparece atrasada.
2. `sudo tsc-sync sync --dry-run`: a diferença deve bater com a do kernel
   (`journalctl -k -b | grep "TSC warp"`, ciclos).
3. `sudo tsc-sync sync`: uma vez, com o sistema ocioso. Depois
   `tsc-sync` deve dizer "sincronizado".
4. Conferir `journalctl -k -b | tail`: avisos do driver `msr` ou do agendador.
5. Jogar uma partida e comparar as paradas com o
   [monitor](https://github.com/rattones/monitor): o spin no núcleo 0 não deve
   mais travar o jogo.
6. Suspender e acordar, depois rodar `tsc-sync`: mostra se o firmware
   dessincroniza de novo ao acordar.
7. Só então `sudo ./install.sh --enable`.

## Riscos e limites

- **É uma gravação no registrador da CPU**, que o próprio Linux evita fazer em
  AMD: numa discussão na lista do kernel (LKML), o mantenedor do código de
  tempo conta que tentativas de corrigir o TSC falharam porque a gravação era
  atrasada pelo SMM do firmware. Por isso o tsc-sync mede de novo depois de
  gravar e só declara sucesso dentro da tolerância.
- **A CPU 0 "pula" para frente uma vez** (no exemplo, 3,39 s). O relógio do
  sistema não é afetado (é o HPET). O relógio interno do agendador na CPU 0
  (`sched_clock`) vê o salto; com o TSC marcado como instável, o kernel filtra
  saltos por CPU, mas pode haver uma contabilidade de tempo de CPU estranha
  naquele instante. Por isso o serviço roda antes dos programas do usuário.
- **O kernel continua no HPET** depois da correção: ele marca o TSC como
  instável no boot e não volta atrás. Não há ganho de desempenho no relógio do
  sistema, só a consistência para quem usa `RDTSC`.
- **O driver `msr` registra um aviso** a cada boot em que grava, e marca o kernel
  como "tainted" (`CPU_OUT_OF_SPEC`): `msr: Write to unrecognized MSR 0x10 by
  tsc-sync ... tainting CPU_OUT_OF_SPEC`. Com Secure Boot e o lockdown do kernel
  ativos, a gravação é recusada (aqui o lockdown está `none`).
- **Desfazer**: `sudo ./install.sh --disable` (ou `--uninstall`) e reinicie. A
  correção feita no boot atual vale até reiniciar; depois o firmware volta a
  deixar o TSC como estava.
- Só faz sentido em máquina física (`ConditionVirtualization=no`).

## Idioma

As mensagens do programa e do `install.sh` seguem o idioma do sistema: inglês ou
português, e qualquer outro idioma cai para o inglês.

```bash
LANG=pt_BR.UTF-8 tsc-sync     # português
TSC_SYNC_LANG=en tsc-sync     # força o inglês, ignorando o locale
```

A detecção segue a precedência POSIX (`LC_ALL` > `LC_MESSAGES` > `LANG`), com
`TSC_SYNC_LANG` acima de todas; `C` e `POSIX` valem inglês. O serviço do
systemd usa o idioma do sistema (`/etc/default/locale`) ao escrever no journal.

O padrão é o mesmo do [monitor](https://github.com/rattones/monitor): o inglês
(`i18n/en.h`) é o idioma de origem e a base, e cada tradução (`i18n/pt.h`)
sobrepõe chave a chave. Uma chave sem tradução sai em inglês, nunca vazia. Os
catálogos são compilados no binário: um programa que roda como root não lê
texto de arquivos em tempo de execução. Como cada mensagem vira formato de
`printf`, o `make check` roda o `i18n_check`, que confere se cada tradução usa
exatamente os mesmos marcadores (`%d`, `%s`, `%+.3f`...) do inglês, na mesma
ordem. Os números saem sempre com ponto decimal, em qualquer idioma. Para
acrescentar um idioma: um `i18n/<idioma>.h`, a tabela dele em `i18n.c` e a
entrada no `i18n_check.c`.

Os comentários do código estão em português.

## Arquivos

| Arquivo | Papel |
|---|---|
| `tsc-sync.c` | o programa (C, só a libc) |
| `i18n.h`, `i18n.c` | detecção de idioma e acesso às mensagens |
| `i18n/en.h` | catálogo em inglês (origem e base) |
| `i18n/pt.h` | catálogo em português |
| `i18n_check.c` | confere os marcadores de cada tradução (`make check`) |
| `systemd/tsc-sync.service` | roda no boot |
| `systemd/tsc-sync-resume.service` | roda ao voltar da suspensão |
| `install.sh` | instala, ativa (`--enable`) ou remove (`--uninstall`) |
| `Makefile` | `make` compila; `make check` confere os catálogos e mede sem root |
