# 3D Connect Four

**[Play Now](https://lucaswade0.github.io/3D-Connect-4/)**

A 3D Connect Four game on a 4x4x4 board. Get four in a row in any direction to win.

## Game Modes

- **vs AI** — Monte Carlo Tree Search opponent (50k iterations)
- **Online Multiplayer** — Peer-to-peer via WebRTC, no server needed. Create a link and send it to a friend.

## Controls

- **Click** a column to drop a piece
- **Arrow keys / Right-drag** to orbit the camera
- **Scroll** to zoom
- **R** to restart (vs AI)

## Tech

- C++ with raylib for the native/WASM AI game
- Three.js + PeerJS for browser multiplayer
- Emscripten to compile C++ to WebAssembly
