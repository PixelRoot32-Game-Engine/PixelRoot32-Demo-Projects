# Roadmap

A top-down GTA for the ESP32 — the handheld-era kind, on a 240x240 panel.
Every planned feature has shipped; what is left is polish and running it on
real hardware.

The handheld Grand Theft Auto games are a design reference here and nothing
more: no asset, name, note or line of code comes from them.

## What you can do today

| | |
|---|---|
| **City** | 128x128 tiles, five districts, two enterable interiors |
| **Driving** | 30 cars, 6 of them in live traffic — all of them steal-able |
| **Crowd** | Pedestrians that use the crossings, police mixed in |
| **Combat** | Pistol and shotgun with finite magazines, a vest, police return fire |
| **Wanted** | Five stars, foot and car pursuit, arrest and respawn |
| **Hiding** | Duck into the corner shop — but the police wait outside if they saw you |
| **Money** | Courier runs pay a fee plus a streak bonus; the shop sells guns, ammo and the vest |
| **Jobs** | Three payphones: boost a named car, hit a marked officer, then a rampage in the Park |
| **Time** | Day/night cycle with lit windows and street lamps after 18:30 |
| **Sound** | Four car-radio stations picked by the car's paint, sirens, 22 sound effects |

A typical run: take a delivery, steal a car out of moving traffic, beat the
clock, bank the streak. Or shoot up the police station, climb to five stars,
get chased on foot and then by patrol cars, go down, and wake up on the
station steps — broke, late and unarmed.

One free pistol lies downtown. Everything after that is bought.

## Shipped

1. **A city that moves without you** — self-driving traffic, pedestrians that
   use crossings.
2. **A reason to shoot** — wanted level, pursuing officers, a police station
   with consequences.
3. **More to do with the city** — courier missions, the shotgun, traffic that
   hurts, patrol cars.
4. **More rooms** — the corner shop, and hiding in it costs the police eight
   seconds of waiting.
5. **Making it look like the hour it is** — lit windows and street lamps on the
   day/night curve.
6. **Sound** — car radios, sirens and a voice budget that never lets a footstep
   drown out a gunshot.
7. **A reason to have money** — the shop counter sells three lines; free guns
   stopped respawning.

## Main missions — all three shipped

The courier run is a side job — it earns money and nothing else. Three main
missions turn that money into progress. They are picked so that each one is a
small rule module and a few banners on top of systems that already exist: no
new interior, no new sprite palette, no dialogue, no save.

**How you start one.** A cyan payphone ring in the street, one per chapter,
shown on the radar the way the green drop already is. Stand on it and RUN
takes the job — the same contextual button that opens a car and a door, tested
after both. Taking a job suspends the courier leg: there is one objective at a
time, because there is one timer plate in the HUD column and no room for a
second.

**Chapter 1 — Boost — done.** The phone rings downtown, eleven tiles from the
spawn. It sends you to a parked car in the Marina, fifty-one tiles away, and
from there to a drop in the Suburbs — one clock for the whole job, so time
saved on the walk is time bought for the drive. Arriving on foot does not
count: the delivery checks the car you are in, which also means losing it is
survivable rather than fatal. Pays 100, which is the pistol plus change — the
chapter that follows needs a gun.

**Chapter 2 — The Hit — done.** A second phone, out in the Suburbs beside the
drop where chapter 1 left you. It sends you seventy-eight tiles to the police
station; the marked officer is the one at the back, eighteen steps in, so you
walk the length of the room past the other two to reach him. Then get out and
shed the stars.

The clock stops when he goes down. That beat already has its pressure — five
stars, six seconds a star, and only while nobody sees you — and a second clock
over the same moment would be two ways to fail that look the same from the
player's chair. Pays 125, which is the gun it took plus most of a vest.


**Chapter 3 — Frenzy — done.** A third phone in the Park, on the busiest
corner the city has — the generator picks it for the walkable ground around
it, because a rampage in a cul-de-sac is a chapter you fail while walking.
There is nothing to find and nowhere to go: the corner is the phone. Answering
it starts a clock with twelve bodies on it.

Twelve because a pistol holds ten and a shotgun holds twelve. The chapter
cannot be finished on one magazine of the cheap gun and can be finished on one
magazine of the expensive one, so "go and buy the shotgun" is a sentence the
player says to themselves — nothing refuses them the phone. Pays 200, the one
fee allowed above the shotgun's price: by the time it pays, they have already
bought it, and there is no chapter after this one for saved money to matter
to.

**Progression.** The chapters unlock in order, and the gear gate is
arithmetic rather than a flag: nothing refuses you a job, the numbers just do
not work out unarmed. Failing one drops you back into the courier loop with
the phone still there — no game over, and no menu to send anybody to. After
the third there is no fourth phone, and the story simply stops.

**What it deliberately leaves out.** No cutscenes or dialogue: the banner
strip is one line and that is the whole vocabulary. No new interior — a room
costs about 1 KB of RAM, 7.5 KB of flash and a scene file, and none of the
three needs one. No mission built around one named pedestrian, because the
crowd is a pool with no way to address one of its twelve. And no saving: the
chapter you are on lives in RAM and resets with the board.

**What the three cost.** One engine-free rule module, now 77 host cases —
the largest suite in the demo, and almost all of it guards; generated payphone
rows with nineteen self-checks standing behind them and the jobs they hand
out; a third RUN context and the banners that go with them. Together: **40 bytes of RAM** and **+3,988 bytes of flash**
— 8.8% RAM and 44.5% flash on `esp32dev`, up 0.3 points from before the
chapters existed.

Chapters 2 and 3 were mostly free of new machinery, which was the point of
shaping chapter 1 the way it was shaped: another phone is a generator row, and
the station, the wanted ladder, the crowd and the hideout were all already
there. What they cost instead was two distinctions, each paid for once and
then reused. Chapter 2 split "a job with a running clock" from "a chapter is
under way at all". Chapter 3 split a third answer off the front of that —
"there is somewhere to point" — because a rampage has a clock and no
destination. Three nested predicates now, each one phase wider than the last,
and every existing call site had to be re-decided against them.

**What the three taught.** Every defect in these chapters was the same shape:
two sources for one fact. The car's live position against the validated one.
Occupancy asked before a move and again after. The chapter index against the
phone table. The walk measured twice, sixteen tiles one way and eighteen the
other. None was a logic error; each was a second copy of something that
already had an answer somewhere else.

Chapter 3 found the same shape in two places none of them had looked. The
HUD's redraw check asked whether a banner was *visible* rather than which
banner it was — two notices in a row had always redrawn once, and nothing had
noticed, because until a counter that fires twelve times in one mission there
had never been two close enough together. That one was older than any of the
chapters.

The other was a coincidence pretending to be a design. The rampage asks for
twelve bodies and the crowd holds twelve people, and those two twelves were
written years apart for unrelated reasons with nothing between them. A player
who cleared the corner and stood still would have waited on corpses fading out
of the pool, on a clock that had not been told the pool existed. Both numbers
are now asserted against each other at build time, which is the only place
they can be: the crowd is an engine actor pool, so no host test can see both
ends of that relationship.


## Not planned, and why

- **Menus, title screen, saves** — this is a demo of a city, not a shipped game.
- **Carrying several weapons** — switching needs a button, and the pad's six
  are spent. It is a design decision waiting on an input, not a memory limit.
- **Health for sale** — the station steps heal for free, and a medkit would make
  the arrest, the only real consequence here, something money switches off.
- **A bigger map** — 128x128 already exceeds what the demo shows off. The
  interesting constraint is the blitter, not the island.

## Measured on hardware

It runs. The build fits in 8.8% RAM and 44.5% flash, and an esp32dev at
240x240 in 12-bit colour holds **25 fps**, steady across the whole sample — so
the guess this section used to carry, "25 fps over a 40 MHz bus", turned out to
be the right number for the wrong reason.

| Stage             | us     | share |
| ----------------- | ------ | ----- |
| Draw              | 21,000 | 52%   |
| Present           | 15,500 | 39%   |
| Update            | 3,500  | 9%    |
| Events, collision | 8      | 0%    |

Present is one `sendBufferScaled`: 4.5 ms packing the 8bpp framebuffer into
RGB444 on the CPU, 9.7 ms blocked on the DMA, 0.3 ms issuing it. 14.7 ms all
told, which on its own would be a 68 fps ceiling.

**The panel is not the constraint.** Draw costs more than Present, which is
the one thing the capture settles outright, and every paragraph in this
repository that treated the SPI transfer as the expensive half was written from
arithmetic rather than from a board. Nothing that touches SPI moves the frame
rate.

What the 21 ms is actually spent on is NOT settled, and the first guess was
wrong. Only Background is a full layer: of the 16,384 cells, Background fills
16,384, Items 1,392 (8.5%) and Details 1,330 (8.1%), and `Renderer::drawTileMap`
skips an index-0 cell before it blits anything. So the three layers are about
68,600 tile pixels per frame, not 172,800 -- Background is 84% of it and the two
overlays together are a sixth. The rest of the 21 ms is the crowd, the traffic,
the vehicles and the HUD, in proportions nobody has measured. Splitting the Draw
timer per section is the next honest step; there is no second guess worth
acting on until then.

**Merging the tilemap layers is not that lever, and is closed off.** The
palette slot is per CELL, not per tile, and the overlays exist precisely to put
a building on a slot the ground underneath it does not use: all 1,392 Item cells
and all 1,330 Detail cells sit on a different slot from their Background. Every
one of them. A composite tile has one slot, so pre-composing means requantising
both halves into a shared 16 colours -- an art rework that would spend the
palette variety the demo is built to show, cost 29 KB of flash (59 KB with the
night set), and buy back only the ~43 overdrawn cells a frame that the overlays
actually cover.

Two more things the run settled:

- `shouldRedrawFramebuffer()` never returns false outdoors. Traffic and the
  crowd move on every frame, so `crowdVisualKey()` changes on every frame, and
  every frame sampled paid a full present. The skip earns its keep in the
  interiors, standing still, and nowhere else. That is not a bug — it is the
  mechanism meeting a scene that always has something moving in it.
- 86,400 bytes in 9.7 ms is about 71 Mbit/s, and a 40 MHz bus cannot carry
  that. Either TFT_eSPI is not clocking at the `SPI_FREQUENCY` define or the
  profiler's `dmaWait` covers only part of the transfer. Until that is
  resolved, there is no headroom on that bus to claim.