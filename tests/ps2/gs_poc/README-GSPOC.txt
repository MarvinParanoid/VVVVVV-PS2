G1 GS renderer feasibility prototypes - OPTIONAL, research only
================================================================
Not part of the game. They draw synthetic VVVVVV-like scenes (generated in
code, no game data) and measure rendering costs. Details:
docs/ps2-gs-renderer-feasibility.md in the repository.

Order (each writes its log into this folder):
 1. gs_poc_sw.elf  software renderer (path A), about 1-2 minutes. It writes
                   gs_poc_sw.log and gs_poc_ref_*.bin. There is no "done"
                   screen; the log ends with END. Then reset the console.
 2. gs_poc_gs.elf  raw GS path (path C), about 30 s. Needs the .bin files
                   from step 1. Writes gs_poc_gs.log and gs_poc_gs_*.ppm.
                   Then reset.

Bring back: gs_poc_sw.log, gs_poc_gs.log, gs_poc_gs_*.ppm.

How to read gs_poc_gs.log: "build" is EE time to write the GS command
packet; "gs" is from the DMA start until the GS signals FINISH, i.e. the
real completion of all drawing (on a console; in PCSX2 it is not
representative). "UPLOAD" lines are EE->VRAM transfers measured to FINISH.
