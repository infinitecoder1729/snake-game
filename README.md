# Snake-Game
> A fun project implementing snake game on different platforms
---
## Check out the HTML implementation at <a href="https://sites.google.com/view/nikhilhanda/snake-game"> my website </a>
---
## For C implementation, read the documentation below:

A high-performance ASCII Snake game engine for the Windows console, written in C and built on the Win32 API.  
It uses double-buffered console rendering, a fixed time-step update loop, input buffering, particle effects, and persistent high scores.

### Features

- Fixed time-step game loop for consistent gameplay independent of frame rate.
- Double-buffered console rendering using Win32 console screen buffers.
- Smooth snake movement with an input queue to avoid missed fast turns.
- Two game modes:
  - Classic mode (open arena)
  - Obstacles mode (procedurally generated walls)
- Dash mechanic:
  - Hold `SHIFT` to move at 2× speed and earn 2× score.
- Combo system:
  - Eat food quickly to increase a combo multiplier up to 4×.
  - Combo decays if you take too long between food pickups.
- Particle system:
  - Simple explosion-like effects when eating food.
- Persistent leaderboard:
  - Top scores saved to `snake_engine.dat` on disk.

### Controls

#### Global / Menus

- In Main Menu:
  - `1` – Start game in Classic mode
  - `2` – Start game in Obstacle mode
  - `H` – Open leaderboard
  - `Q` – Quit
- In Leaderboard:
  - `ESC` – Return to main menu
- Global:
  - `F3` – Toggle debug mode (if enabled in code)

#### In-Game

- Movement:
  - Arrow keys **or** `W`/`A`/`S`/`D`
- Dash:
  - Hold `SHIFT` to dash (2× speed & 2× score)
- Pause:
  - `P` – Toggle pause
- Exit to menu:
  - `ESC` – Return to main menu

#### Game Over / Name Entry

- Type letters `A–Z` to enter your name (up to 10 characters).
- `BACKSPACE` – Delete last character.
- `ENTER` – Confirm name and save score to leaderboard.

#### Build & Run 

This project targets Windows and uses the Win32 console API, so it needs to be compiled with a Windows C toolchain. No external libraries are required beyond the Windows SDK headers and standard C library. I have included a `.exe` file to enable a direct run.

### Save Data / Leaderboard

- High scores are stored in a binary file:
  - `snake_engine.dat`
- The file keeps a small leaderboard (top N scores) with:
  - Player name
  - Score
  - Max combo achieved

You can safely delete `snake_engine.dat` to reset the leaderboard; it will be recreated on next run.

#### Possible Improvements

Some ideas you could explore:

- Add configurable settings (speed, map size, colors).
- Add more obstacle patterns or multiple levels.
- Add sound effects via WinMM or another audio library.
- Refactor into multiple translation units (engine, game, UI, persistence) for easier maintenance.
---
