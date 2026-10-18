# Somnium server

Laptop FastAPI brain for Project Somnium. The image server runs on NixOS, inside
this repo's Python flake. The repo [README](../README.md) covers flashing, Wi-Fi,
the USB bridge, and creative config.

A camera on the same 2.4 GHz LAN POSTs `multipart/form-data` to
`/v1/process` and reads the dream JPEG back. `./scripts/server.sh` binds
`0.0.0.0:8080` and advertises `somnium.local` (mDNS A record plus
`_somnium._tcp`). `SOMNIUM_MDNS=0` turns advertising off.
`SOMNIUM_ADVERTISE_IP` pins the address in the A record when the outbound-route
guess is wrong. The USB bridge still posts to `127.0.0.1`.

`source=wifi` is stored on the shot JSON. `skip_gallery=1` skips the laptop
gallery hook so a camera that has its own bearer token does not upload twice.

Saved shots go to `server/outputs/`. `output_dir` in
[`settings.json`](settings.json) is `outputs`, and the pipeline resolves that
relative to the `server/` directory.

## Gallery auto-upload

Each shot can be sent to the gallery. Nothing is uploaded unless both
environment variables are set:

| Variable | Value |
| --- | --- |
| `SOMNIUM_UPLOAD_URL` | `https://www.somnium.photo/api/upload` |
| `SOMNIUM_UPLOAD_TOKEN` | Gallery shared secret, sent as `X-Somnium-Token` |

The POST body is `multipart/form-data` with three file fields:

| Field | File |
| --- | --- |
| `orig` | `*_0_orig.jpg` |
| `dream` | `*_1_dream.jpg` |
| `meta` | `<stem>.json` |

The camera server uploads a shot itself when both variables are set in its
environment. The hook starts after the metadata JSON is written, on a daemon
thread with a 20 second timeout. It logs success or failure and does not delay
the shot or change the USB response. With that hook enabled, the directory
watcher is optional.

`server/scripts/somnium_uploader.py` is the other path: it polls
`server/outputs` and uploads each finished trio. Use the watcher when the
server was started without the variables, or to backfill files already on
disk. Running the hook and the watcher together uploads each new shot twice.

The token below is a placeholder. Put the real token only in a mode `600` file
that is sourced into the shell and never committed. `~/.config/somnium/upload.env`
sits outside the repo. The gitignored `.env` is the other place `scripts/server.sh`
will load; if the token lives there, `chmod 600 .env`.

### NixOS

```sh
install -d -m 700 ~/.config/somnium
cat > ~/.config/somnium/upload.env <<'EOF'
export SOMNIUM_UPLOAD_URL=https://www.somnium.photo/api/upload
export SOMNIUM_UPLOAD_TOKEN=your-token-here
EOF
chmod 600 ~/.config/somnium/upload.env
source ~/.config/somnium/upload.env
```

Replace `your-token-here`, then from the repository root either enter the
dev shell and watch `server/outputs`:

```sh
nix develop
python server/scripts/somnium_uploader.py watch server/outputs
```

or run one of these directly:

```sh
nix develop -c python server/scripts/somnium_uploader.py watch server/outputs
nix run .#uploader
```

`nix run .#uploader` is the `apps.x86_64-linux.uploader` flake output. It runs
the same watch command against `server/outputs`. The uploader imports only the
Python standard library (`urllib`), so the dev shell needs no extra modules.

Source the same file in the shell that starts `./scripts/server.sh` when you
want the in-process hook instead of the watcher.

### macOS / Linux

```sh
export SOMNIUM_UPLOAD_URL="https://www.somnium.photo/api/upload"
export SOMNIUM_UPLOAD_TOKEN="your-token-here"
```

### Windows PowerShell

The image server runs on NixOS, not Windows. These lines only set the same
variables:

```powershell
$env:SOMNIUM_UPLOAD_URL = "https://www.somnium.photo/api/upload"
$env:SOMNIUM_UPLOAD_TOKEN = "your-token-here"
```
