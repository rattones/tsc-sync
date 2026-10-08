/*
 * en.h - English catalog. This is the SOURCE language: every key exists here,
 * and i18n.c uses it as the base layer under every translation. A key missing
 * from a translation falls back to the English text.
 *
 * Conventions (same as the monitor's catalogs):
 *   - Keys are <module>_<what>: usage_, measure_, result_, sync_, err_.
 *   - Placeholders are printf-style, in the order the code passes them.
 *   - Never put translated text in a fixed-width slot ("%-10s"): the padding
 *     counts bytes, so accented text would misalign the column.
 *   - Numbers always use a dot as the decimal separator (the program does not
 *     call setlocale for LC_NUMERIC), so output stays comparable across
 *     languages.
 *
 * Each line: MSG(key, "text")
 */
MSG(usage,
    "usage: tsc-sync [measure | sync [--dry-run]] [--tol-us N] [--quiet]\n"
    "  measure          measure each CPU's TSC offset (default)\n"
    "  sync             write the TSC of CPUs outside the majority group (root)\n"
    "  sync --dry-run   show what it would write, without writing\n"
    "  --tol-us N       tolerance for \"in sync\", in microseconds (default 50)\n"
    "  --quiet          only errors and the final result\n"
    "language: TSC_SYNC_LANG, LC_ALL, LC_MESSAGES or LANG (en, pt)\n")
MSG(err_need_root,       "sync needs root (to only see what it would do: sync --dry-run)\n")
MSG(err_affinity,        "could not move to CPU %d: %s\n")
MSG(measure_single_cpu,  "only one CPU: nothing to compare\n")
MSG(measure_header,      "TSC at %.3f MHz; tolerance %.0f us; measurement uncertainty ~%.1f us\n")
MSG(measure_reference,   "reference: CPU %d (majority group)\n")
MSG(measure_ok,          "ok")
MSG(measure_behind,      "BEHIND")
MSG(measure_ahead,       "AHEAD")
MSG(result_synced,       "result: in sync\n")
MSG(result_out_of_sync,  "result: %d CPU(s) out of sync\n")
MSG(sync_would_write,    "CPU %d: would write TSC + %lld cycles (%+.6f s)\n")
MSG(sync_write_failed,   "CPU %d: failed to write MSR 0x10 (%s); modprobe msr? kernel lockdown?\n")
MSG(sync_pass,           "CPU %d: pass %d, %+.3f us left\n")
MSG(sync_no_converge,    "CPU %d: did not converge (%+.3f us left)\n")
MSG(sync_done,           "CPU %d: in sync (%+.3f us left)\n")
