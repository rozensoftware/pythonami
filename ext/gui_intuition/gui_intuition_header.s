; 2026 by Piotr Rozentreter (Rozsoft)
;
; First object on the vlink line: places Py68ExtHeader at the start of the
; first CODE hunk. After LoadSeg, pythonami reads it at BADDR(seg)+4.

        section "CODE",code

        xref    _gui_intuition_exports
        xdef    _py68_ext_header

_py68_ext_header:
        dc.l    $50593638           ; PY68_EXT_MAGIC 'PY68'
        dc.w    1                   ; PY68_EXT_ABI_VERSION
        dc.w    25                  ; export_count (must match table)
        dc.l    _gui_intuition_exports
