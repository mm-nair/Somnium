# Somnium server

FastAPI brain for Project Somnium. Locally it runs on the laptop, inside this
repo's Python flake. The same process can run on Fly.io so a shot does not
need the laptop powered on. The repo [README](../README.md) covers flashing,
the USB bridge, and creative config.

Saved shots go to `server/outputs/` unless `SOMNIUM_OUTPUT_DIR` is set. On Fly
that directory is `/data/outputs` on a persistent volume. `output_dir` in
[`settings.json`](settings.json) is `outputs`, and a relative path is resolved
next to this directory.

## Why Fly.io

The server is one long HTTP request, not a WebSocket or a serial port. `POST
/v1/process` reads the JPEG, asks Open-Meteo for the weather, asks xAI to
rewrite the prompt (timeout 90s), asks xAI to edit the image (timeout 240s),
writes the original, the dream, and a JSON sidecar, then returns the dream
JPEG. A daemon thread may POST that trio to the gallery for another 20
seconds. Surprise mode remembers the last variation pick in memory and in
`.somnium-variations.json` under the output directory.

Vercel already hosts the gallery, and it is the wrong place for this process.
A cook can run longer than a function's comfortable window, the disk on a
function disappears when the request ends, and the gallery thread is still
running when the JPEG is returned. Restructuring onto Blob and a queue would
be a different server. Fly runs the existing uvicorn process:

- HTTPS on `https://<app>.fly.dev` (shared IPv4, no extra address charge)
- one always-on `shared-cpu-1x` machine with 512 MB, so Pillow can decode the
  larger edit without running out of memory
- a 1 GB volume for the shot files
- `GET /health` for the platform check
- proxy idle timeout of 600 seconds, because a cook sends no bytes until the
  JPEG is ready and Fly's default is 60 seconds

Cost for a new Fly account: there is no free tier. The trial is 2 machine-hours
or 7 days, whichever ends first, and trial machines stop after a few minutes,
so it is only a smoke test. After a card is on file, this machine is about
$3.69 per 30 days in Ashburn (`iad`) or Secaucus (`ewr`), and a bit more in
Seattle (`sea`, the default in `fly.toml`). The 1 GB volume is about $0.15 per
month. Egress in North America is $0.02 per GB. A camera shot is a few hundred
kilobytes, so bandwidth stays small. Budget about $4 a month if the machine
stays up. Do not scale past one machine: the volume attaches to a single
Machine, and a second copy would not see the files.

`auto_stop_machines` is off. Waking a stopped machine on the shutter press adds
a cold start in front of an already slow edit, and it can kill the gallery
upload thread.

## Deploy

Install [flyctl](https://fly.io/docs/flyctl/install/) and `fly auth login`.
From the repository root:

1. Pick a globally unique app name. `fly.toml` says `somnium-camera`. If that
   name is taken, change the `app` line. The public URL is
   `https://<app>.fly.dev`.
2. Create the app and the volume in the same region as `primary_region`
   (`sea` unless you edit it):

```sh
fly apps create somnium-camera
fly volumes create somnium_data --size 1 --region sea -a somnium-camera
```

3. Set secrets. These are not written to `fly.toml` or git. Generate the
   process token yourself (`openssl rand -hex 32`). It is a different secret
   from the gallery token.

```sh
fly secrets set -a somnium-camera \
  XAI_KEY='xai-...' \
  SOMNIUM_PROCESS_TOKEN='paste-a-long-random-token' \
  SOMNIUM_UPLOAD_URL='https://www.somnium.photo/api/upload' \
  SOMNIUM_UPLOAD_TOKEN='gallery-token'
```

`XAI_KEY` and `SOMNIUM_PROCESS_TOKEN` are required. `fly.toml` sets
`SOMNIUM_REQUIRE_AUTH=1`, and the process exits on boot if the process token
is missing. Omit the two `SOMNIUM_UPLOAD_*` secrets to keep shots on the
volume without posting them to the gallery. Optional venue overrides, if the
camera does not send lat/lon:

```sh
fly secrets set -a somnium-camera \
  SOMNIUM_VENUE_LAT='47.6553' \
  SOMNIUM_VENUE_LON='-122.3035' \
  SOMNIUM_VENUE_LABEL='Seattle'
```

4. Deploy from the repository root (the Dockerfile builds the image):

```sh
fly deploy -a somnium-camera
curl -fsS https://somnium-camera.fly.dev/health
```

`/health` is public and includes `"auth": true` when the process token is
set. `POST /v1/process` without `X-Somnium-Token` or `Authorization: Bearer`
returns 401 and does not call xAI. OpenAPI docs are off while the token is set.

A deploy restarts the machine and drops an in-flight cook. Shot files on the
volume remain. If a gallery upload was still in its 20 second window, backfill
from the volume (the secrets are already in the machine environment):

```sh
fly ssh console -a somnium-camera -C \
  "python /app/server/scripts/somnium_uploader.py watch /data/outputs --once"
```

Prompt text lives in `creative/creative.json` inside the image. Editing it
means a new `fly deploy`. To try a file without rebuilding, set
`SOMNIUM_CREATIVE_PATH` to a copy on the volume.

### Local container

Same image, no Fly account. `SOMNIUM_REQUIRE_AUTH` is unset here, so a token
is optional. Set one if you want to see the 401.

```sh
docker build -t somnium-server .
docker run --rm -p 8080:8080 \
  -e SOMNIUM_PROCESS_TOKEN=dev-token \
  -e XAI_KEY \
  -v somnium-outputs:/data \
  somnium-server
```

`-e XAI_KEY` forwards the variable from the shell. Do not bake it into the
image. `GET http://127.0.0.1:8080/health` should return `"ok": true`.

### Laptop clients

`./scripts/server.sh` is unchanged for a laptop bind: with no
`SOMNIUM_PROCESS_TOKEN`, `/v1/process` stays open. To send USB cooks at the
Fly host instead of localhost, put these in `.env` (mode `600`, never commit)
and start the bridge:

```sh
SOMNIUM_SERVER_URL=https://somnium-camera.fly.dev/v1/process
SOMNIUM_PROCESS_TOKEN=paste-a-long-random-token
SOMNIUM_PROCESS_TIMEOUT=360
```

`scripts/usb_bridge.py` and `scripts/usb_cooktest.py` read those. `--url` and
`--timeout` still override them. `./scripts/cook.sh photo.jpg --http` uses the
same URL and sends the same header. The USB cable still needs the laptop.
Laptop-off shooting needs the Wi-Fi firmware below.

## ESP32 on Wi-Fi

This branch does not change the board. The open pull request
[Wi-Fi cook path with USB serial fallback](https://github.com/mm-nair/Somnium/pull/6)
(`cursor/wifi-camera-upload-dafc`) is the transport. It already POSTs the same
multipart body to a host the camera stores in NVS, with compile-time defaults
in `include/server_config.h`:

| Setting | Remote value |
| --- | --- |
| `SOMNIUM_SERVER_HOST` | `somnium-camera.fly.dev` |
| `SOMNIUM_SERVER_PORT` | `443` |
| `SOMNIUM_SERVER_PATH` | `/v1/process` |
| `SOMNIUM_SERVER_TLS` | `1` |

The setup portal's host field wins over the header after a save. `somnium.local`
and the laptop mDNS advertiser in that PR only work on the same LAN. A Fly
hostname does not need them. The board's cook timeout is 300 seconds
(`kCookTimeoutMs`). Fly's idle timeout is 600 seconds, so the proxy is not
what ends a normal edit. The Wi-Fi client uses `WiFiClientSecure` with
certificate checks off, which is enough to reach Fly's certificate.

That PR does not send a process secret. Before a public host will accept a
shot, `HttpCook`'s cook POST has to add one header on the request it already
builds (`Content-Type` / `Accept` in `HttpCook.cpp`):

```
X-Somnium-Token: <SOMNIUM_PROCESS_TOKEN>
```

`Authorization: Bearer <SOMNIUM_PROCESS_TOKEN>` is also accepted. Store the
token in NVS the way that PR already stores the gallery token, and do not
commit it. The gallery token (`SOMNIUM_GALLERY_TOKEN` / `SOMNIUM_UPLOAD_TOKEN`)
is a different secret for `somnium.photo`. Sending it to Fly will 401.

Until that header exists on the board, leave the camera's gallery URL empty
and let Fly upload, or the reverse. That PR also sends `skip_gallery=1` so
the two uploads do not double-post. This server ignores unknown form fields,
including `source` and `skip_gallery`, so merging the Wi-Fi branch later does
not require a change here for the cook itself. After both exist, a shot with
`skip_gallery=1` would still be uploaded by this server until that field is
honored. Use only one uploader until then.

`GET /health` stays unauthenticated, which matches that PR's health check
before the cook.

## Environment

| Variable | Role |
| --- | --- |
| `XAI_KEY` | xAI key for prompt and image edits. `XAI_API_KEY` is accepted as an alias. |
| `SOMNIUM_PROCESS_TOKEN` | Shared secret for `POST /v1/process`. Unset means open, which is the laptop default. |
| `SOMNIUM_REQUIRE_AUTH` | `1` on Fly. The process refuses to start without the token. |
| `SOMNIUM_SERVER_URL` | Bridge and cook client base URL or full `/v1/process` URL. |
| `SOMNIUM_PROCESS_TIMEOUT` | Bridge HTTP timeout in seconds. Default 240. Use 360 for a remote cook. |
| `SOMNIUM_HOST` / `SOMNIUM_PORT` | Bind address. `PORT` is used if `SOMNIUM_PORT` is unset. |
| `SOMNIUM_OUTPUT_DIR` | Shot directory. Relative paths are still next to `server/`. |
| `SOMNIUM_VENUE_LAT` / `SOMNIUM_VENUE_LON` / `SOMNIUM_VENUE_LABEL` | Override `settings.json` when the request has no coordinates. |
| `SOMNIUM_CREATIVE_PATH` / `SOMNIUM_SETTINGS_PATH` | Override the JSON files baked into the image. |
| `SOMNIUM_UPLOAD_URL` / `SOMNIUM_UPLOAD_TOKEN` | Gallery upload. Both must be set or the hook does nothing. |

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
