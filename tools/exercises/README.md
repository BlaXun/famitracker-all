# The exercises' starter songs

Help > Exercises opens its starter songs and the First steps' scripts from
`res/exercises`, built into FamiTracker.exe. These scripts write them, with
Python 3 alone (no FamiTracker and no other tools needed), taking the
instruments of the first example song; from the repository's root:

    python3 tools/exercises/make_starters.py res/examples/first-song.ftm res/exercises
    python3 tools/exercises/make_first_steps.py res/examples/first-song.ftm res/exercises
    python3 tools/exercises/first_steps_scripts.py res/exercises

They write those files byte for byte as they are; the other scripts there
(the Next steps', `first-song.txt` and `song-shape.txt`) aren't written by
them. After a change, `TestExerciseStarters` in `Source/Api/tests` checks
every starter.
