# UI fork: decision record and capture checklist

Recorded 2026-11-12, against upstream v1.9.2 (commit `bfecfd2`).

Status (2026-11-12, evening): built and installed. The fork runs on the PCH-1000 as
`VPNG00001` beside the stock app. The design is in `UI-SPEC.md`. Measured results and
what is still unverified are in "Results on the Vita" at the end of this file.
Nothing is committed.

## Goal

Replace the text-only Vita screen with a refined UI: a live DualShock 4 mirror,
touch zones for L1/R1/L3/R3, and clean stats. Target hardware is one PCH-1000
(OLED), used over Wi-Fi with the DualShock 4 client on Windows.

## Decisions

| # | Decision | Why it matters, with evidence |
|---|---|---|
| 1 | Own fork, `0xmrwn/VitaPad-NG`. | Upstream has no licence and issues are off. |
| 2 | Daily use is Wi-Fi, DualShock 4 client on Windows. | USB zones are hardcoded separately ([usbmode.c:44-55](Server/source/usbmode.c)). |
| 3 | The Vita decides the touch zones. | The client already maps the L1/R1/L3/R3 bits ([ViGEm.cpp:190-193](Client/ViGEm.cpp)). |
| 4 | PCH-1000 OLED only. | Burn-in drives decisions 9, 10, 12, 13, 15, 16. |
| 5 | The main screen is a DualShock 4 mirror. | Sprites are PNG, authored as SVG; vita2d cannot render SVG. |
| 6 | The mirror shows what the PC receives. | That is the value after fast buttons, remap and PS ([main.c:123](Server/source/main.c)), plus zone bits. |
| 7 | PS button and hold combos stay on the frame clock. | They run once per frame ([main.c:368-387](Server/source/main.c)), so steady 60 fps is a requirement. |
| 8 | Latency budget: small, if justified. | Proposed limits are below; final numbers follow the baseline. |
| 9 | The picture fades to black when idle. | Black is the existing screen-off state ([main.c:394-395](Server/source/main.c)). |
| 10 | **Revised 2026-11-12 after seeing it on the Vita:** during play the controller stays on screen, dimmed to 60%, with pressed parts lit on top. | The first version (black screen, only pressed parts) looked wrong on the device. The picture still fades to black after 2 minutes without input. |
| 11 | Zones follow the thumbs; the picture is feedback only. | Today's zones are four full quadrants ([usbmode.c:49-52](Server/source/usbmode.c), [ViGEm.cpp:56-63](Client/ViGEm.cpp)). |
| 12 | Zone hint shows briefly, then fades. | Triggers: a new PC connection, and the first touch after a quiet period. |
| 13 | The full lit body shows only before a PC connects. | The TCP flag marks a connected PC ([main.c:150](Server/source/main.c), [main.c:166](Server/source/main.c)); the client keeps it alive while streaming ([Client/main.cpp:77-78](Client/main.cpp)). |
| 14 | Front view kept; the existing SVG is extended, not redrawn. | L1 and L2 need a convention, since the front view shows one sliver per side. |
| 15 | **Revised 2026-11-12:** two inner areas of the rear panel are touch zones for L1/L3 (left hand) and R1/R3 (right hand); the rest of the rear panel is dead. Motion still lights nothing. | Rear presses reach the PC as button bits, so the mirror lights them like any other press. Rear touch points are no longer forwarded unless `RearTouch=Forward`. |
| 16 | Live stats appear on demand, then fade. | Hold combos reach the PC as presses ([main.c:123](Server/source/main.c), [main.c:368-372](Server/source/main.c)), so the trigger should not be a button combo. |
| 17 | Vita app only. | Client, protocol ([Common/protocol.h](Common/protocol.h)) and kernel module stay untouched. |
| 18 | Installs beside the stock app under its own title ID. | The ID is set in [CMakeLists.txt:15](Server/CMakeLists.txt) and [psbutton.c:14-17](Server/source/psbutton.c). |
| 19 | Own use first. | The controller artwork is used as is, logos included. Name and licence questions are deferred. |
| 20 | The stats panel is a short health summary. | The full readout stays as a diagnostic view for A/B comparison. |

### Latency budget (decision 8), proposed limits

- Button path: at most 1 ms added, or twice the baseline noise, whichever is larger.
- Stream: longest gap under 20 ms, packets per second within baseline noise ([TESTING.md:134](TESTING.md)).
- Fast buttons: 0 fallbacks ([TESTING.md:123](TESTING.md)).
- Frame rate: no tolerance on 60 fps.
- "Justified" means the cost is tied to one visible feature that can be switched off.

### Short health summary (decision 20)

- Packets per second to the PC ([main.c:423](Server/source/main.c)).
- Fast buttons: lead time and fallbacks ([main.c:431-432](Server/source/main.c)).
- Frame time. No instrument exists yet.
- Battery. Read at [main.c:360](Server/source/main.c), not drawn today.

## Defaults assumed, not explicitly decided

- A Wi-Fi dropout brings the full body back only after a few seconds without a PC.
  The flag can take up to 3 seconds to clear ([main.c:27](Server/source/main.c)).
- The fork uses its own settings file. The stock app drops unknown keys when it
  saves ([remap.c:101-111](Server/source/remap.c)).
- On the PC, swap-shoulders stays off ([windows.xml:10-11](Client/windows.xml)). `VIGEM_FRONT_TOUCH`
  no longer matters: the fork sends zone presses as button bits and no front touch points
  (`UI-SPEC.md` section 5.2).
- Idle ignores motion and rear touch.
- The stats trigger is a tap in a centre dead area, if the thumb capture leaves one.

## Controller artwork

Source: three SVGs in `~/Documents/Codex/2026-11-12/generate-a-svg-of-a-playstation/outputs`.
They are used as is, logos included.

Checked on `dualshock-4.svg`, rendered at Vita height with `resvg` and `rsvg-convert`:

- 773x485 px on the 960x544 screen, about 93 px free on each side.
- Face button 61 px, stick with base 157 px.
- Transparent background; no filters, fonts or embedded images.
- The two rasterisers differ by about 1.4%.
- Average frame brightness: DualShock 4 12%, DualSense 37%, Xbox 360 48%.

Still missing in the SVG:

- Lit or pressed variants of every part.
- A second shoulder sliver per side, for L1 and L2.
- The stick cap separated from its well, and a distinct look for L3/R3.
- Names for the D-pad arms, the two sticks, and Share versus Options.

## Captures on the Vita, in order

All of these use the stock v1.9.2 build and need no code.

1. **Baseline numbers.** Record the on-screen readouts ([main.c:420-433](Server/source/main.c))
   and the `--monitor` packets per second and longest gap. Do it once with the main
   screen and once with the screen off (hold L + R + SELECT). Settles the noise
   level and the final latency budget.
2. **Thumb reach and resting contact.** Save `--monitor` output to a file. Sweep
   each thumb slowly over everything it reaches in your normal grip, then play for a
   few minutes without touching on purpose. The monitor prints five lines a second
   ([Client/main.cpp:1096](Client/main.cpp)) in 1920x1088 touch units. Settles the
   zone shapes, the centre dead area, and whether fingers rest on the rear panel.
3. **Stick rest noise.** Leave the Vita untouched for a minute under `--monitor`.
   Settles the idle dead band.
4. **The dark body on the OLED.** Render the SVG to 960x544 and open it on the Vita:

   ```bash
   resvg -h 544 dualshock-4.svg ds4.png
   ```

   ```bash
   magick ds4.png -background black -gravity center -extent 960x544 ds4-vita.png
   ```

   Settles whether the near-black shading is legible.
5. **System dimming.** While connected and idle, note when the screen dims and
   turns off, and whether the first press afterwards reaches the PC.
6. **Screenshots of today's states:** main screen, remap menu, not connected.

## Needs code, so it waits

- Frame time, and whether the buffer swap blocks.
- Draw cost of full-screen blended sprites.
- Free video memory and PNG load time.
- Real touch panel dimensions (`SceTouchPanelInfo`).
- Whether the connected flag flips on every client reconnect.

## Deferred until a public release

- A name not confusable with VitaPad.
- A written licence answer from the upstream maintainer.

## Results on the Vita (2026-11-12)

Measured from the Mac with `tools/vita_monitor.py` (stream mode, 30 s runs) and screenshots
taken through the `vita-use` tool. The same Mac and network were used for both apps.

| Measure | Stock v1.9 | Fork |
|---|---|---|
| Packets per second, screen on | 157.4 | 159.3 and 158.2 (two runs) |
| Packets per second, stock screen off | 158.7 | not applicable |
| Gap between packets, median | 6.34 ms | 6.34 and 6.35 ms |
| Gap between packets, 95th percentile | 9.88 ms | 9.80 and 9.63 ms |
| Lost packets | 0 | 0 |
| Frame rate (PSVshell counter) | 60 | 60 in setup and in play |
| CPU load per core (PSVshell) | 21 / 11 / 22 / 14 % | setup 21 / 11 / 6 / 14 %, play 22 / 10 / 11 / 16 % |

- Every run, stock and fork alike, shows about two gaps of roughly 90 ms per second. They
  are present with the stock screen off too, so they come from the Wi-Fi link or this Mac,
  not from drawing. The longest-gap limit of 20 ms can only be judged from the Windows PC.
- Stick rest noise (capture 3): each axis moves by at most 2 counts. The left stick rests at
  about (127, 135). The dead band of 16 and the activity threshold of 24 hold.
- Setup screen: renders pixel-aligned from the real framebuffer; the atlas loads; text fits.
- Play at rest: the frame is pure black.
- Buttons: all face buttons, D-pad, Start, Select, L and R reach the Mac with the right bits.
- Install: the VPK installs and launches beside `VPAD00001`; the kernel module is shared.

Hands-on run with `tools/vita_wizard.py` (2026-11-12, evening, build with the dimmed play body):

- Touch zones: top left, top right, bottom left and bottom right send L1, R1, L3 and R3, each alone.
- A thumb sliding from top left to bottom right keeps L1 until it lifts.
- Three centre taps open the summary, the diagnostic view and close it; none sends a button to the PC.
- No front touch point was forwarded in 34,243 packets.
- Lit parts (Cross, L2, D-pad right, the L1 band, the right stick ring, a deflected stick cap) were
  confirmed on the OLED by eye and in screenshots.
- Diagnostic view on the device: 159 packets per second; frame 16.68 ms average, 16.77 ms maximum,
  0 late; fast buttons 6.1 ms sooner over 6 changes, 241 reads per second, 0 fallbacks.
- The "work" figure of the frame instrument reads 16.65 ms: it includes the wait for the display,
  so it does not measure CPU work. Use the frame period and the late count only.

Thumb reach (capture 2, stock app, one 95 s recording):

- Left thumb: screen x 40 to 410, y 50 to 490. Right thumb: x 560 to 955, y 40 to 510 (pixels).
- A centre strip about 145 px wide (x 415 to 560) is touched by neither thumb.
- No accidental front touch while pressing buttons. Fingers rest on the rear panel almost constantly.
- The built-in stats ellipse (x 360 to 600) overlaps both thumbs' reach by about 40 px on each side.
- Fixed with `Server/assets/zones-pch1000.txt`, copied to `ux0:data/VitaPadNG/zones.txt`: the stats
  ellipse is now x 425 to 535, y 152 to 392. Checked on the device: the hint outline spans x 426 to 533.
  The built-in default table is unchanged. Delete the file on the Vita to go back.

Rear touch zones (added the same evening, from an annotated photo of the back):

- Built-in table, touch units as seen from the front: L1 451,0 to 941,544; L3 451,544 to 941,1088;
  R1 979,0 to 1469,544; R3 979,544 to 1469,1088. Outer margins and the centre gap are dead.
- A file `ux0:data/VitaPadNG/rear-zones.txt` replaces the table (same grammar as `zones.txt`);
  `Server/assets/rear-zones-pch1000.txt` is the built-in table in that form.
- Guided check on the device: left upper, right upper and right lower sent L1, R1 and R3 alone.
  Left lower sent L3, but L1 was seen first; the user was not yet used to the rear pad, so this is
  not counted as a defect. Watch whether the top/bottom split at half height suits the left hand.
- 40 seconds of normal play with sticks and buttons: 0 accidental rear presses.
- Lying on a table: no button, and no rear touch point forwarded.

Still unverified:

- The longest packet gap, from the Windows PC (this Mac's link shows periodic stalls).
- Capture 5 (system dimming) and how the dimmed play body ages on the OLED over long sessions.
- Wi-Fi dropout behaviour, USB modes, suspend and resume.

Notes for testing with `vita-use`: the Vita's L and R are `l1` and `r1`; SELECT with UP or
DOWN toggles the PSVshell overlay; the console's own sleep timer still runs on the setup screen.

