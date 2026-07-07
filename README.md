# TvtTape Plugin
Controls HDV equipment and outputs the TS stream to BonDriver_Pipe.dll.

## Architecture (TvtPlay-style split)

- `TvtTape.cpp` / `TvtTape.h`
  - Main plugin class (`CTvtTape`)
  - Status-item UI, timer update loop, command dispatch
- `VcrDevice.cpp` / `VcrDevice.h`
  - VCR unit controller class (`CVcrDevice`)
  - Device enumeration, transport control, timecode read
- `PipeControl.cpp` / `PipeControl.h`
  - BonDriver_Pipe control channel adapter (`CPipeControl`)
  - `PAUSE`, `PURGE`, `GET_READY_STATE`

## Current features

- TVTest plugin output: `TvtTape.tvtp`
- Transport controls in status item:
  - Stop
  - Play
  - Pause
  - Rewind (toggle: Rewind -> Play)
  - FastForward (toggle: FastForward -> Play)
- Status display:
  - Current transport state
  - TimeCode (`HH:MM:SS:FF`)
- Manual VCR device selection:
  - Click `TvtTape Device` status item
  - `Auto select` or explicit device index
  - Selection persisted in `TvtTape.ini`
- BonDriver_Pipe integration:
  - Pause/Resume synchronization
  - Purge on stop

## Todo
- [ ] Improving UI usability

## Build

```powershell
msbuild .\TvtTape.vcxproj /p:Configuration=Release /p:Platform=x64
```

If `msbuild` is not in PATH, run from Visual Studio Developer Command Prompt.
