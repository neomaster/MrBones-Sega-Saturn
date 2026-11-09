# RUNWAY reconstruction evidence

The shipped disc 1 track 01 SHA-256 is
`1edf45a5ef5496291a9bf268266067590cb21e3f4930fa6279c105198b6f1f73`.
RUNWAY `03CODE` SHA-256 is
`7532e4b8e804c6908ce03c987587612876a65663e56fa32b2174d022da9dc216`.

The RUNWAY linker map places `.stxtr` at 0x25c08000 with length 0x56110,
matching `/04RUNWAY/04STXT`. The `02TITL` file matches the combined lengths of
the `.ttxtr1` and `.ttxtr2` sections, and begins with the map's title pointers.
These facts identify content; they do not establish how pixels are rendered.

The STXTR parser is based on a complete static inspection of all 24 nonempty
`04STXT` files in the two supplied discs (6,093 records including the two MENU
copies). Each observed record has big-endian width and height, an unknown
32-bit field, a 16-bit payload length in four-byte units, and marker
`12 34 56 78 9a bc`; records start on 16-byte boundaries. RUNWAY has 316
records and ends at 0x56110. This is observed for these dumps, not a guarantee
about other versions. The parser does not interpret colors or palettes.

In the RUNWAY `03CODE`, 308 of 316 record start addresses have references
with nearby VDP1 descriptor fields: 176 indicate 256-color mode (PMOD mode 4)
and 132 indicate RGB mode (mode 5), with width/height matching CMDSIZE. The
other eight were not found by this literal-pointer search. The palette,
transparency, orientation and actual draw path still require validation.
An experimental texel converter supports the observed modes with a caller-selected
palette and specific PMOD flags; it does not implement palette-bank selection,
blending, mesh, flips or full VDP1 composition. Palette layout and transparency
remain unverified against live gameplay.
Static analysis of `_PalLoad` identifies the first word of `_MainPalette`
as the starting palette index. Color words begin at RUNWAY `03CODE+0x1ff54`,
not at the table address itself; the background diagnostic uses a hypothesized
fourth bank at `03CODE+0x20b54`. The selected bank is not validated in play.
`pan_palette_apply` reconstructs the palette stream operations and preserves
existing colors for zero words; this is also a static reconstruction.

`runway_sprite_resolve` associates a mapped RUNWAY descriptor address with
its STXTR record and validates dimensions, payload length and color mode. It
does not determine which sprites are visible, their positions or palette bank.
`runway_project_center` reconstructs the center and bounds check in
`_RWorldSprite` at 0x00268a64; rotation, size, animation and draw commands
are not implemented and the result has not been checked against execution.

`05SND1` contains 12 big-endian offset/size pairs in the first 0x60 bytes.
All 24 nonempty sound packages observed in the supplied discs have entries
within file bounds. `snd1_parse` returns these 12 spans; the sample codec,
sound driver and playback behavior have not yet been identified.
`mb_sound_start_sequence` adapts the slot-selection logic at 0x002041d0;
its callback is an interface adaptation and does not produce audio.
Component 0 of `05SND1` is an event table: a big-endian count and offset
table followed by 32-byte entries, with bit 31 of the first word marking the
last entry. `mb_sound_event_open/next` expose the raw words; their meanings
are not yet confirmed. This matches the static `_SoundEvent` analysis and
was checked against 24 nonempty sound packages.
The `cdda_read_sector` conversion treats track bytes as little-endian stereo
PCM, consistent with continuity measurements on the supplied disc 1 track 2.
It has not been verified by listening or linked to any RUNWAY event.

`bones_change_state` is a native adaptation of RUNWAY `_BonesChangeState`
at 0x0025d484, from Ghidra SH-2 disassembly and pseudocode. Its state context
and callbacks are inferred interfaces: their actual side effects and the
correspondence to game structures remain unvalidated in execution. The same
adaptation includes `_BonesFirstState` (0x0025d460), `_BonesSetLatch`
(0x0025c0c4), `_BonesClearLatch` (0x0025c0f4) and `_BonesKillVelAccel`
(0x0025e088). Their field names are hypotheses. Their tests
exercise the adapted transition logic, not gameplay equivalence.

`mb_mover_step` adapts RUNWAY `CMover::Move` at 0x00259d28: it adds the
velocity to each position component, then acceleration to each velocity
component, using modulo-2^32 arithmetic. The role of this mover in player
control, the coordinate scale and timing remain unverified.

`mb_bones_move_message` adapts seven original position-update handlers at
0x00265294–0x00265354; the axes mask is a native API choice, not an original
message ID. `mb_message_dispatch` adapts the mask/value table dispatch at
0x00265054 with an explicit bound; argument types, message IDs and producers
are still unknown and these functions do not implement player controls.

`runway_input_step` adapts one loop iteration at 0x0026a614 using raw
`_BonesInput` bytes. Callback semantics, scheduler and movement units remain
unverified; it is not a playable controller. The original also calls a yielding
scheduler after each iteration, which is the caller's responsibility here.
`saturn_pad_update` adapts the physical input read at loader address
0x0020bf08 and tracks held, released and pressed bits. The button mapping is
based on the Saturn pad documentation.
`host_input_keyboard_buttons` supplies a host keyboard → `MB_PAD_*` bitset for
Windows; `mbvisual` wires it through `saturn_pad_update_host` into
`runway_input_step`. Layout and timing are host choices, not original bindings.
`runway_compose_*` blits an opaque background and alpha-tested sprites into an
RGBA framebuffer for diagnostics only; it is not a VDP1 command-list renderer.

REA (this continuation): map ZIP inventory 28/27
(`ev_1bc4e0cc…` / `ev_c57e6ac6…`); loader entropy 6.5493
(`ev_9c31f5ef…`); RUNWAY `03CODE` entropy 5.9903 (`ev_46ee295a…`);
extracted RUNWAY `05SND1` SHA-256 `1e0a8ff4…d97968`, entropy 7.3880 with
multiple high runs (`ev_d7130db6…`). Bundle:
`Neomaster\mr-bones-rea\session-evidence.json`. Windows REA+Ghidra P0 does not
admit SH-2; SH-2 work stays on local Ghidra.

`pan_collision_decode` adapts the RLE prefix of `_NewDecompFrame` at
0xc0000064. In RUNWAY's first `15PAN` record, it consumes 200 bytes and
outputs 4,480 bytes (SHA-256
`6e4b078619065b1761e8c95122c665ed073941f7f92c56e108d9e7813ab6d67e`).
The destination corresponds to `_pPanCollMap` in the linker map. The
meaning of its values and the remaining frame format are not established.
`pan_frame_decode` is an experimental reconstruction of the tile operations
of that frame format. It processes 320x224 indexed planes and does not select a
palette. A bounded offline check decoded 2,649 plausible RUNWAY records, but
the buffer sequence and visuals are not validated against the original game.

The loader's `.data` mapping needs further investigation: the map locates
`_SoundFunctionList` at 0x002143e0, while the expected pointer sequence was
observed at file offset 0x143b0. Do not treat every flat-file offset as a
validated linked address.
The RUNWAY call literal at 0x002697e0 points to 0x0020bf08, which begins
a controller-read function in the supplied loader bytes, while the bundled
loader map labels 0x0020bf34 as `_SMPCgetPad`, inside that function. The
loader map must therefore not be applied blindly: previously assigned loader
symbols and translated routines require independent address revalidation.
The loader binary's likely `_CreateLoader` start is 0x0020b834 rather than
the map's 0x0020b860. Earlier functions `_SoundEvent` and
`_SoundStartSequence` start at their mapped addresses with plausible
prologues and literal references, so the divergence is not a single global
offset. These observations are static, not validation in execution.
