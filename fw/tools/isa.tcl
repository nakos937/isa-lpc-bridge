# openocd helpers (source this in an openocd telnet/gdb session, probe attached, firmware running):
#   source tools/isa.tcl
#   isa_inb 0x220        ;# ISA I/O read through the mailbox, no LPC host needed
#   isa_outb 0x226 0x00  ;# ISA I/O write
#   isa_irq              ;# 165 readout (bit0 IOCS16# (0 = asserted), 1-10 IRQ4-7,9-12,14,15, 11 IOCHCK# (0 = asserted), 12 IRQ3)
#   isa_drq              ;# DRQ bits (bit n = DRQn)
#   fw_set t_cmd_ns 800  ;# live tuning, see src/cfg.h
#   fw_stats             ;# counters
set FW_DBG 0x2007C000
set FW_CFG_FIELDS {magic 0x00 build 0x04 t_cmd_ns 0x08 t_addr_setup_ns 0x0C t_hold_ns 0x10 t_rdy_timeout_ns 0x14 chain_nops 0x18 flags 0x1C sysclk_khz 0x20 irq_hold_us 0x24 t_drq_ns 0x28 irq_frames 0x2C t_cs16_ns 0x30 t_cmd8_ns 0x2040 t_dma_cmd_ns 0x2044 t_dack_setup_ns 0x2048 t_dack_hold_ns 0x204C}
# fw_dbg_t: cfg (0x2050 bytes) + stats (0x48) + mbox (0x18) + trace_head
set ST_OFF 0x2050
set MB_OFF [expr {$ST_OFF + 0x48}]
proc fw_rd {off} { global FW_DBG; return [read_memory [expr {$FW_DBG + $off}] 32 1] }
proc fw_wr {off v} { global FW_DBG; mww [expr {$FW_DBG + $off}] $v }
proc fw_set {name v} { global FW_CFG_FIELDS; fw_wr [dict get $FW_CFG_FIELDS $name] $v }
proc fw_stats {} {
    global ST_OFF
    set names {ior iow dma_r dma_w rdy_timeouts denied lpc_words stale_tx irq_changes ldrq_sent irq_stream drq last_addr last_data last_type rx_overflow_hint loops resets}
    set i 0
    foreach n $names { puts [format "%-16s 0x%08x" $n [fw_rd [expr {$ST_OFF + 4*$i}]]]; incr i }
}
proc mbox {cmd a0 a1} {
    global MB_OFF
    fw_wr [expr {$MB_OFF + 4}] $a0
    fw_wr [expr {$MB_OFF + 8}] $a1
    set seq [fw_rd [expr {$MB_OFF + 0x14}]]
    fw_wr $MB_OFF $cmd
    for {set i 0} {$i < 200} {incr i} {
        if {[fw_rd [expr {$MB_OFF + 0x14}]] != $seq} { break }
        sleep 5
    }
    return [fw_rd [expr {$MB_OFF + 0x0C}]]
}
proc isa_inb {a}      { return [format 0x%02x [mbox 1 $a 0]] }
proc isa_outb {a d}   { mbox 2 $a $d; return }
proc isa_irq {}       { return [format 0x%04x [mbox 4 0 0]] }
proc isa_drq {}       { return [format 0x%02x [mbox 5 0 0]] }
proc isa_reset {ms}   { mbox 6 $ms 0; return }
