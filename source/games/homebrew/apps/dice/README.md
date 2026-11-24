# Dice

Rockbox's Dice by Brandon Low, from `apps/plugins/dice.c`, GPL-2.0-or-later.

Rolls up to twelve dice with 3, 4, 6, 8, 10, 12, 20 or 100 sides and shows them as solids drawn with the host's OpenGL ES, with their total. Six-sided dice are cubes with pips, 8 an octahedron, 10 a pentagonal trapezohedron, 12 a dodecahedron and 20 an icosahedron. A d4 is a tetrahedron read at its top corner; once the dice rest, the view tilts back so that corner can be read. A d3 is a cube numbered 1 to 3 twice. A d100 is a percentile pair, a ten-sided die marked 00 to 90 beside one marked 0 to 9, 00 and 0 reading 100; it takes two places, so up to six are rolled.

| Control | |
|---|---|
| Center | roll again, or end a roll that is still settling; in a list, choose the row |
| Wheel, Previous, Next | move through a list |
| Menu | from the roll or a setting list, back to the main list; from the main list, quit |

The list offers Roll Dice, Number of Dice, Number of Sides, Animation and Quit. A roll drops the dice onto the felt one after another; each spins as it falls, tips over as it bounces and rocks to a stop with its result facing up, in a random order, over one to two and a half seconds. Animation Off shows the result at once. The settings are kept between starts.

| | |
|---|---|
| Data | one record in the host's save store, `dice` in the app's data folder: the number of dice and of sides and whether rolls animate |
| Heap | 320 KiB: the label atlas, drawn at start, GL's 128 KiB copy of it, and a 150 KiB copy of the screen with the dice that rest, which frames copy instead of drawing those dice again |
| Minimum eApp SDK | 1.0 |
