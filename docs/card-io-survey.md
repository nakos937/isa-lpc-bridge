# Word I/O in Linux ISA sound drivers (checked 2026-11-11)

Sources: torvalds/linux master `sound/isa`, `include/sound`; v4.14 `sound/oss`, `drivers/scsi`.
Grep: inw/outw/insw/outsw/inl/outl/insl/outsl/readw/writew/ioread16/iowrite16.

Byte I/O only: adlib, ad1816a, ad1848, als100, azt2320, cmi8328, cmi8330, cs423x, es1688, es18xx, galaxy,
opl3sa2, opti9xx, sb (except emu8000), sc6000, sscape, wss; OSS aedsp16, trix, sb_*, mpu401, uart401/6850, opl3.

Word I/O:
- sb/emu8000.c (AWE32/64): inw/outw on 0x620/0xA20/0xA22/0xE20/0xE22 (reads and writes).
- gus/gus_io.c, gus_pcm.c: inw/outw/outsw on base+0x104 (GF1DATALOW / InterWave I16DP).
- wavefront_synth.c (Tropez, Maui): outw to base+4 and base+6 only ("16 bit, writeonly").
- oss/pss.c (ADI/Echo PSS: Orchid SoundWave 32, Cardinal DSP16): inw/outw on base+0, +2, +4, base 0x220 or 0x240 (reads and writes).
- oss/pas2_mixer.c (PAS16 rev D): one outw to mixer port - 1 (write).
- SCSI on sound cards: aha152x (AIC-6360, SB16 SCSI-2) insw/outsw on base+0x16; g_NCR5380 53C400 insw/outsw.

Memory window, not I/O: msnd (Turtle Beach MultiSound, Pinnacle, Fiji): readw/writew through devm_ioremap of a
32 KB ISA memory window. The bridge has no memory cycles.

Not PC ISA cards: oss/waveartist.c (NetWinder), oss/kahlua.c (Cyrix MediaGX SMI memory).
