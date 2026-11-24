# 2048

Rockbox's clone of 2048 by Franklin Wei, from `apps/plugins/2048.c` and its tile artwork, with the high score table of `apps/plugins/lib/highscore.c` by Linus Nielsen Feltzing, GPL-2.0-or-later.

Slide the tiles across a four by four board. Two tiles with the same number that touch merge into one and add their sum to the score. Reach the 2048 tile to win; the game goes on until no move is left. One move can be taken back.

| Control | |
|---|---|
| Touch the top, right, bottom or left of the wheel, without clicking | slide the tiles that way, once per touch |
| Center | take back the last move |
| Menu | pause: resume, new game, high scores, help, quit with or without saving |

A quit, and the host ending the game, keep the board for the next start; the main menu then offers Resume Game. The five best scores are kept with the biggest tile reached.

| | |
|---|---|
| Saves | one record in the host's save store, `2048` in the game's data folder: the board to resume and the five best scores |
| Heap | 256 KiB |
| Minimum eApp SDK | 1.0 |
