# Somnium server

FastAPI brain for Project Somnium. Locally it runs on the laptop, inside this
repo's Python flake. The same process can run on an Oracle Cloud Always Free
VM so a shot does not need the laptop powered on. The repo
[README](../README.md) covers flashing, the USB bridge, and creative config.

Saved shots go to `server/outputs/` unless `SOMNIUM_OUTPUT_DIR` is set. On the
VM that directory is `/data/outputs` on a Docker volume on the boot disk.
`output_dir` in [`settings.json`](settings.json) is `outputs`, and a relative
path is resolved next to this directory.

## Why Oracle Cloud Always Free

The server is one long HTTP request. `POST /v1/process` can sit for a few
minutes while xAI works, then writes the shot and may upload it in the
background. The camera in [PR #8](https://github.com/mm-nair/Somnium/pull/8)
reaches it with HTTPS on port 443, a hostname, and `X-Somnium-Token`.

That needs a process that stays running, a disk that survives reboots, and no
proxy that cuts a silent response off at one or two minutes. Oracle's Always
Free Ampere A1 VM is that machine, and the resources below are **$0** for the
life of the account. They are not the 30-day $300 trial credit. Do not upgrade
the account to Pay As You Go unless you mean to. Always Free resources stay
free after an upgrade too, but anything past the limits is billed.

Sources, checked against the current docs:

- [Always Free Resources](https://docs.oracle.com/en-us/iaas/Content/FreeTier/freetier_topic-Always_Free_Resources.htm): Ampere A1 is 2 OCPUs and 12 GB total (1,500 OCPU hours and 9,000 GB hours per month). Up to two AMD `VM.Standard.E2.1.Micro` instances, each 1/8 OCPU and 1 GB. Instances must be in the home region. 200 GB of block storage, boot volume included, default boot volume 50 GB. 10 TB of outbound data per month. Idle instances may be reclaimed.
- [Free Tier FAQ](https://www.oracle.com/cloud/free/faq/): a credit or debit card is required to prove identity. Oracle may place an authorization hold. The hold is not a charge. Always Free continues after the trial credits expire. An account idle for 30 days may be treated as abandoned.
- [Changing payment method](https://docs.oracle.com/en-us/iaas/Content/Billing/Tasks/changingpaymentmethod.htm): signup authorizes the card for $1, and Oracle reverses that authorization. The bank decides when the hold disappears.

Use one Ampere A1 instance: 1 OCPU, 6 GB RAM, Ubuntu, a public subnet, and the ephemeral public IPv4 that the launch form offers. 6 GB leaves half of the free memory unused and is enough for Python and a decoded edit. Build the image on the VM. The Dockerfile uses the official Python image, which publishes `linux/arm64` and `linux/amd64`, and the pinned wheels exist for both. If the console says "out of host capacity", try another availability domain, then the AMD micro shape. The micro shape has 1 GB of RAM, which is tight for an image edit; add a swap file on the boot disk before relying on it.

### What does not fit

| Host | Why it is not the one |
| --- | --- |
| Google Cloud `e2-micro` | Ongoing free tier, not a trial: one non-preemptible `e2-micro` in `us-west1`, `us-central1`, or `us-east1`, plus 30 GB-months of disk ([Free Tier limits](https://docs.cloud.google.com/free/docs/free-cloud-features)). It only continues after the trial if the project is on a paid billing account. Past the limits, including 1 GB of North America egress per month, is billed. 1 GB of RAM is the same squeeze as the AMD micro. Usable if Oracle has no capacity. |
| Render free | [Spins down after 15 minutes](https://render.com/docs/free) without a request, takes about a minute to wake, and the filesystem is lost on spin-down. Persistent disks are paid. A shutter press would wait on a cold start, and saved shots disappear. |
| Koyeb free | [One free instance](https://www.koyeb.com/docs/reference/instances): 512 MB, 0.1 vCPU, 2 GB SSD, Frankfurt or Washington only. It scales to zero after 1 hour without traffic, and free instances cannot attach a volume, so the disk does not survive idle. |
| Cloudflare Tunnel from home | The tunnel is free, but proxied HTTP has a [proxy read timeout of 125 seconds](https://developers.cloudflare.com/fundamentals/reference/connection-limits/) (HTTP 524). Only Enterprise zones can raise it. A cook is silent until the JPEG is ready and the board waits 300 seconds, so the tunnel would cut it off. The laptop would also have to stay on. |
| Fly.io | No ongoing free tier. Left in git history only. |

## Deploy

1. Create an Oracle Cloud Free Tier account. Use a real credit or debit card (not a prepaid or single-use card). Expect a $1 authorization hold, not a charge. Pick the home region nearest the camera. It cannot be changed later, and the free VM has to live there. Do not click through to Pay As You Go.

2. Create a VM instance:

   - Image: Ubuntu, the one marked Always Free Eligible. Skip Oracle Linux Cloud Developer. That image wants at least 8 GB, and this VM is 6 GB.
   - Shape: `VM.Standard.A1.Flex`, 1 OCPU, 6 GB. A1 can be created in any availability domain except South Korea North (Chuncheon). If the console says "out of host capacity", try another availability domain, then `VM.Standard.E2.1.Micro` (1 GB; add a swap file on the boot disk before relying on it).
   - Networking: public subnet, assign a public IPv4. Use that ephemeral address. Do not create extra reserved public IPs and leave them unattached.
   - Boot volume: leave the default (about 50 GB). It counts toward the 200 GB Always Free block storage. Do not add another volume.
   - Add your SSH key.

3. In the subnet security list, allow ingress TCP 80 and TCP 443 from `0.0.0.0/0`. Leave SSH restricted to your own IP if you can. Do not open 8080. The app is only on the Docker network.

4. SSH in, then open the instance firewall. Oracle's Ubuntu image rejects packets the security list already allowed. Docker publishes 80 and 443 on the FORWARD chain, and that image rejects FORWARD with `icmp-host-prohibited`, so an INPUT rule alone does not let the ports through. The reject can sit on `iptables` or on `iptables-legacy` (whichever backend the image used). `deploy/open-http-ports.sh` accepts 80 and 443 on INPUT and deletes that FORWARD reject on both. Do not `iptables-save` while Docker is running: the `DOCKER` chains are not present at boot, and a saved ruleset breaks the next start. `somnium.service` runs the script again on boot.

```sh
sudo apt-get update
sudo apt-get install -y docker.io docker-compose-v2 git
sudo usermod -aG docker "$USER"
```

Log out and back in so the docker group applies. Then:

```sh
git clone https://github.com/mm-nair/Somnium.git /opt/somnium
cd /opt/somnium
sudo bash deploy/open-http-ports.sh
```

5. Secrets. `openssl rand -hex 32` for the process token. This is not the gallery token.

```sh
cd /opt/somnium
cp deploy/.env.example deploy/.env
chmod 600 deploy/.env
```

Fill `SOMNIUM_HOSTNAME`, `SOMNIUM_ACME_EMAIL`, `SOMNIUM_PROCESS_TOKEN`, and `XAI_KEY`. `SOMNIUM_REQUIRE_AUTH=1` is set by Compose. The process exits if the token is empty, so a public VM cannot boot open.

6. A hostname. Either is free. Set `SOMNIUM_HOSTNAME` to the name you pick.

   - DuckDNS, and it survives an IP change: create a name at [duckdns.org](https://www.duckdns.org/), put `DUCKDNS_DOMAIN` (the name only, not `.duckdns.org`) and `DUCKDNS_TOKEN` in `deploy/.env`, then run `deploy/duckdns-update.sh`. Put it in cron if you want (`*/30 * * * *`).
   - No account: `sslip.io` names the current address. `140.238.10.20` becomes `140-238-10-20.sslip.io`. [An ephemeral public IP stays assigned when the instance stops](https://docs.oracle.com/en-us/iaas/Content/Network/Tasks/managingpublicIPs.htm) and is deleted when the instance is terminated. If that happens, the name changes and the camera config has to change with it.

7. Start it, and start it again on boot:

```sh
cd /opt/somnium/deploy
set -a
. ./.env
set +a
docker compose up -d --build
docker compose ps
curl -fsS "https://${SOMNIUM_HOSTNAME}/health"
sudo cp /opt/somnium/deploy/somnium.service /etc/systemd/system/somnium.service
sudo systemctl enable --now somnium.service
```

`/health` is public and includes `"auth": true`. `POST /v1/process` without `X-Somnium-Token` or `Authorization: Bearer` returns 401 and does not call xAI. API docs are off while the token is set.

Caddy obtains the certificate over port 80 and redirects callers to 443. `response_header_timeout` is 360 seconds, past the board's 300 second limit, so the proxy is not what ends the cook. The response is buffered (`flush_interval -1`) and sent with `Content-Length`. The board does not accept a chunked body. Shot files are in the `somnium_outputs` volume. Backfill the gallery from there with:

```sh
docker compose exec somnium \
  python /app/server/scripts/somnium_uploader.py watch /data/outputs --once
```

A prompt edit is a `docker compose up -d --build` from `/opt/somnium` after you pull.

### Idle reclaim

Oracle may reclaim an Always Free instance when, over 7 days, CPU utilization at the 95th percentile is under 20% and network utilization is under 20%. On A1, memory utilization under 20% is a third condition, and all three have to be true together. This process on a 6 GB VM sits well under 20% memory, so memory does not protect it. A few short cooks can also stay under the CPU and network lines. A lightly used VM can be taken back. Do not add a CPU burner. The account can also be closed if nobody signs in for 30 days. Sign in now and then.

### Laptop clients

`./scripts/server.sh` with no `SOMNIUM_PROCESS_TOKEN` is still the open laptop server. To send USB cooks at the VM:

```sh
SOMNIUM_SERVER_URL=https://somnium.duckdns.org/v1/process
SOMNIUM_PROCESS_TOKEN=<the same process token>
SOMNIUM_PROCESS_TIMEOUT=240
```

The bridge default is 240 seconds. A value at or above the board's 300 second wait is capped to 290, so the laptop can still write `ERR` before the board gives up. Caddy's 360 seconds is the proxy budget, not this client timeout. The USB cable still needs the laptop.

## ESP32

[PR #8](https://github.com/mm-nair/Somnium/pull/8) is the Wi-Fi cook. This branch does not change firmware. In the gitignored `include/server_config.h` from that PR:

| Setting | Value |
| --- | --- |
| `SOMNIUM_SERVER_HOST` | `somnium.duckdns.org` (or the sslip.io name) |
| `SOMNIUM_SERVER_PORT` | `443` |
| `SOMNIUM_SERVER_PATH` | `/v1/process` |
| `SOMNIUM_SERVER_TLS` | `1` |
| `SOMNIUM_PROCESS_TOKEN` | the same secret as `deploy/.env` |

An empty host still means the laptop bridge and `somnium.local`. A non-empty host makes the board POST the cook itself. It sends `X-Somnium-Token` when the process token is non-empty. TLS checks the Let's Encrypt certificate against the ISRG roots baked into the firmware and sets the clock from NTP first. A cook with no clock, or a certificate that does not chain to those roots, fails closed. Caddy still presents a real Let's Encrypt certificate.

When that firmware also has a gallery URL and gallery token, the cook includes `skip_gallery=1` and this server does not upload the same shot. Leave the camera gallery settings empty to let the VM upload. The gallery token is a different secret from the process token.

## Environment

| Variable | Role |
| --- | --- |
| `XAI_KEY` | xAI key. `XAI_API_KEY` is accepted as an alias. |
| `SOMNIUM_PROCESS_TOKEN` | Shared secret for `POST /v1/process`. Unset means open, which is the laptop default. |
| `SOMNIUM_REQUIRE_AUTH` | `1` in Compose. The process refuses to start without the token. |
| `SOMNIUM_HOSTNAME` | Name Caddy certificates. Set in `deploy/.env`. |
| `SOMNIUM_ACME_EMAIL` | Let's Encrypt account email. |
| `SOMNIUM_SERVER_URL` | Bridge and cook client base URL or full `/v1/process` URL. |
| `SOMNIUM_PROCESS_TIMEOUT` | Bridge HTTP timeout in seconds. Default 240. Values at or above 300 are capped to 290. Caddy waits 360. |
| `SOMNIUM_HOST` / `SOMNIUM_PORT` | Bind address inside the container. `PORT` is used if `SOMNIUM_PORT` is unset. |
| `SOMNIUM_OUTPUT_DIR` | Shot directory. `/data/outputs` on the VM. |
| `SOMNIUM_VENUE_LAT` / `SOMNIUM_VENUE_LON` / `SOMNIUM_VENUE_LABEL` | Override `settings.json` when the request has no coordinates. |
| `SOMNIUM_CREATIVE_PATH` / `SOMNIUM_SETTINGS_PATH` | Override the JSON files baked into the image. |
| `SOMNIUM_UPLOAD_URL` / `SOMNIUM_UPLOAD_TOKEN` | Gallery upload. Both must be set or the hook does nothing. |
| `DUCKDNS_DOMAIN` / `DUCKDNS_TOKEN` | Optional. Used only by `deploy/duckdns-update.sh`. |

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
watcher is optional. A cook that sends `skip_gallery=1` (the Wi-Fi camera
does this when it uploads the shot itself) does not start the hook.

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
