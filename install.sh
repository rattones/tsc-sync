#!/usr/bin/env bash
#
# install.sh - instala o tsc-sync e as units do systemd, SEM ativar nada.
#
#   sudo ./install.sh              # compila e instala; o servico fica desativado
#   sudo ./install.sh --enable     # instala e ativa (boot e volta da suspensao)
#   sudo ./install.sh --disable    # desativa, mas mantem instalado (para medir)
#   sudo ./install.sh --uninstall  # desativa e remove
#
# Ordem recomendada antes do --enable: medir (tsc-sync), ver o que gravaria
# (sudo tsc-sync sync --dry-run), gravar uma vez a mao (sudo tsc-sync sync) e
# conferir (tsc-sync). O efeito de uma gravacao dura ate o proximo boot: para
# desfazer, desative o servico e reinicie.
#
# Mensagens em ingles ou portugues, pela mesma regra do programa: a primeira de
# TSC_SYNC_LANG, LC_ALL, LC_MESSAGES e LANG; "C", "POSIX" e outros idiomas
# ficam em ingles. Sao poucas, entao ficam aqui mesmo, num case por chave.

set -uo pipefail
SRC="$(cd -- "$(dirname -- "$(readlink -f -- "${BASH_SOURCE[0]}")")" && pwd)"
BIN=/usr/local/sbin/tsc-sync
UNITS=(tsc-sync.service tsc-sync-resume.service)
DOC=/usr/local/share/doc/tsc-sync
ARGS=("$@")

# --- idioma -----------------------------------------------------------------------
raw="${TSC_SYNC_LANG:-${LC_ALL:-${LC_MESSAGES:-${LANG:-}}}}"
raw="${raw%%.*}"; raw="${raw%%@*}"; raw="${raw%%_*}"
L=en; [[ "${raw,,}" == pt ]] && L=pt

# t <chave> [args...]: a mensagem no idioma detectado, com os %s expandidos.
t() {
  local k="$1" f; shift
  case "$L:$k" in
    pt:usage) f='uso: sudo %s [--enable | --disable | --uninstall | --help]\n  (sem opcao)   compila e instala; nada e ativado\n  --enable      instala e ativa no boot e na volta da suspensao\n  --disable     desativa, mas mantem o tsc-sync instalado (para medir)\n  --uninstall   desativa e remove\n' ;;
    *:usage)  f='usage: sudo %s [--enable | --disable | --uninstall | --help]\n  (no option)   build and install; nothing is enabled\n  --enable      install and enable at boot and on resume\n  --disable     disable, but keep tsc-sync installed (to measure)\n  --uninstall   disable and remove\n' ;;
    pt:err)        f='erro: %s\n' ;;
    *:err)         f='error: %s\n' ;;
    pt:need_root)  f='rode com sudo: sudo %s' ;;
    *:need_root)   f='run with sudo: sudo %s' ;;
    pt:bad_opt)    f='opcao desconhecida: %s (veja --help)' ;;
    *:bad_opt)     f='unknown option: %s (see --help)' ;;
    pt:need_gcc)   f='precisa do gcc (sudo apt install build-essential)' ;;
    *:need_gcc)    f='gcc is required (sudo apt install build-essential)' ;;
    pt:no_build)   f='nao compilou' ;;
    *:no_build)    f='build failed' ;;
    pt:removed)    f='removido. O TSC volta ao estado do firmware no proximo boot.\n' ;;
    *:removed)     f='removed. The TSC goes back to the firmware state at the next boot.\n' ;;
    pt:installed)  f='instalado: %s e %s\n' ;;
    *:installed)   f='installed: %s and %s\n' ;;
    pt:enabled)    f='ativado: roda no proximo boot e a cada volta da suspensao.\npara rodar agora: sudo systemctl start tsc-sync.service\n' ;;
    *:enabled)     f='enabled: runs at the next boot and on every resume.\nto run it now: sudo systemctl start tsc-sync.service\n' ;;
    pt:disabled)   f='desativado: nao roda mais no boot nem na volta da suspensao.\nO tsc-sync continua instalado (para medir: tsc-sync). A correcao feita neste\nboot vale ate reiniciar; depois disso a CPU volta ao estado do firmware.\n' ;;
    *:disabled)    f='disabled: no longer runs at boot or on resume.\ntsc-sync is still installed (to measure: tsc-sync). The fix applied in this\nboot lasts until you reboot; after that the CPU goes back to the firmware state.\n' ;;
    pt:not_installed) f='o tsc-sync nao esta instalado (rode: sudo %s)' ;;
    *:not_installed)  f='tsc-sync is not installed (run: sudo %s)' ;;
    pt:next)       f='\nNada foi ativado. Para testar e depois ativar:\n  tsc-sync                       # mede (sem root)\n  sudo tsc-sync sync --dry-run   # o que gravaria\n  sudo tsc-sync sync             # grava uma vez, a mao\n  tsc-sync                       # confira: "sincronizado"\n  sudo %s --enable               # ativa no boot e na volta da suspensao\n' ;;
    *:next)        f='\nNothing was enabled. To test, then enable:\n  tsc-sync                       # measure (no root)\n  sudo tsc-sync sync --dry-run   # what it would write\n  sudo tsc-sync sync             # write once, by hand\n  tsc-sync                       # check: "in sync"\n  sudo %s --enable               # enable at boot and on resume\n' ;;
    *) f="<$k>" ;;
  esac
  # shellcheck disable=SC2059
  printf -- "$f" "$@"
}

die() { t err "$1" >&2; exit 1; }

case "${1:-}" in
  -h|--help) t usage "$0"; exit 0 ;;
esac
(( EUID == 0 )) || die "$(t need_root "$0 ${ARGS[*]}")"

case "${1:-}" in
  ""|--enable) ;;
  --disable)
    # So as units: o binario fica, para continuar medindo. disable --now nao
    # desfaz nada no processador (o TSC corrigido fica ate o reboot); so tira os
    # servicos do boot e da volta da suspensao.
    [[ -e "/etc/systemd/system/${UNITS[0]}" ]] || die "$(t not_installed "$0")"
    systemctl disable --now "${UNITS[@]}"
    t disabled
    exit 0 ;;
  --uninstall)
    systemctl disable --now "${UNITS[@]}" 2>/dev/null
    rm -f "$BIN" "${UNITS[@]/#//etc/systemd/system/}"; rm -rf "$DOC"
    systemctl daemon-reload
    t removed
    exit 0 ;;
  *) die "$(t bad_opt "$1")" ;;
esac

command -v gcc >/dev/null 2>&1 || die "$(t need_gcc)"
gcc -O2 -Wall -Wextra -o "$SRC/tsc-sync" "$SRC/tsc-sync.c" "$SRC/i18n.c" || die "$(t no_build)"
# O binario fica com dono do usuario que chamou o sudo dentro do projeto.
[[ -n "${SUDO_USER:-}" ]] && chown "$SUDO_USER:" "$SRC/tsc-sync"

install -o root -g root -m 0755 "$SRC/tsc-sync" "$BIN"
for u in "${UNITS[@]}"; do
  install -o root -g root -m 0644 "$SRC/systemd/$u" "/etc/systemd/system/$u"
done
install -d -m 0755 "$DOC"
for d in README.md README.en.md; do
  [[ -r "$SRC/$d" ]] && install -m 0644 "$SRC/$d" "$DOC/$d"
done
systemctl daemon-reload
t installed "$BIN" "${UNITS[*]}"

if [[ "${1:-}" == --enable ]]; then
  systemctl enable "${UNITS[@]}"
  t enabled
else
  t next "$0"
fi
