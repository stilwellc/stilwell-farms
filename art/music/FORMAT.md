# Stilwell Farms music format (.mml)

One file per song in `art/music/NAME.mml`. Compiled by `tools/gen_music.py` into a tiny GBC driver.

```
song SPRING tempo=7 loop=yes        # tempo = frames per 16th note (60 fps): 6=150bpm 7=128 8=112 9=100 10=90
ch1 duty=2 env=a2                    # square 1 (lead). duty 0..3 (12.5/25/50/75%), env=<vol 0-f><decay 0-7>
o5 l8 c e g >c< | b4 g4 ...
ch2 duty=1 env=63                    # square 2 (harmony/arpeggio)
...
ch3 wave=bass vol=2                  # wave channel: wave=bass|soft|organ|pluck, vol 1(loud)..3(quiet)
...
ch4                                  # noise drums
k8 h8 s8 h8 | ...
```

Notes (ch1-ch3): `c d e f g a b`, `+` or `#` sharp, `-` flat, `r` rest. Length after the note in
note-values: 1 2 4 8 16 (and `.` dotted), e.g. `c4 d8. e16`; without a number the current `l` is used.
`o3`..`o6` set octave (o4 = middle C octave), `>` up an octave, `<` down. `l8` sets default length.
`&` ties to the next note (`c4&c8`). `[ ... ]3` repeats the bracket 3 times (nestable once).
`|` is a bar line and is ignored. `v7`..`vf` changes the channel volume (high hex digit of env) from
that point on. Shortest note is a 16th.

Drums (ch4): `k` kick, `s` snare, `h` closed hat, `o` open hat, `r` rest, with lengths like notes.

Every channel in a song must have the SAME total length (the compiler checks), it loops from the
start when `loop=yes`. A channel line may be omitted (silent). Keep songs 8-32 bars; they loop.

Style: cozy, melodic, chiptune pastoral (original melodies only, nothing borrowed). Lead on ch1,
counter-melody / arpeggios on ch2, bass on ch3 (wave=bass), light drums on ch4.
Songs to write: TITLE, SPRING, SUMMER, FALL, WINTER, NIGHT (calm, slow, used after 8pm), TOWN,
FESTIVAL, and short non-looping jingles (loop=no, 1-3 bars): JINGLE_SLEEP (lullaby cadence),
JINGLE_FANFARE (milestone/letter), JINGLE_BUY (land purchase / big buy), JINGLE_CATCH (caught a fish).
