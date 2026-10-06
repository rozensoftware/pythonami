; 2026 by Piotr Rozentreter (Rozsoft)
;
; First object on the vlink line: places Py68ExtHeader at start of first CODE hunk.

        section "CODE",code

        xref    _asl_exports
        xdef    _py68_ext_header

_py68_ext_header:
        dc.l    $50593638           ; PY68_EXT_MAGIC 'PY68'
        dc.w    1                   ; PY68_EXT_ABI_VERSION
        dc.w    1                   ; export_count
        dc.l    _asl_exports
