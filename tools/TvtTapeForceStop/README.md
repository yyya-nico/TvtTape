# TvtTapeForceStop

TvtTape が残した VCR デバイスに対して、外部から Stop と必要に応じて電源断を送るための CLI ツールです。

## 使い方

```powershell
TvtTapeForceStop.exe --list
TvtTapeForceStop.exe --all
TvtTapeForceStop.exe --index 0
TvtTapeForceStop.exe --name "Panasonic"
TvtTapeForceStop.exe --all --stop-only
```

## 挙動

- `IAMExtTransport::put_Mode(ED_MODE_STOP)` を送る
- 既定では続けて `IAMExtDevice::put_DevicePower(ED_POWER_OFF)` も試す
- `--with-power-off` を付けると電源断も送る
- `--list` で対応デバイスを列挙する

## 注意

- 対応デバイスが `IAMExtTransport` / `IAMExtDevice` を公開している場合に有効です
- ドライバ側が Stop / PowerOff を実装していない場合は止めきれません
