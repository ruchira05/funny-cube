# key-ai

ESP32 raw audio -> faster-whisper (STT) -> Ollama (short reply + emotion) -> JSON.

## Ubuntu deploy

```bash
git clone <repo> key-ai && cd key-ai    # or scp the folder
chmod +x setup.sh && ./setup.sh
```

`setup.sh` installs Ollama (official installer, systemd service), pulls the model,
builds `.venv`, and runs the API as the `key-ai` systemd service on `127.0.0.1:8000`.

Optional: `LLM_MODEL=phi3:mini WHISPER_MODEL=base.en ./setup.sh`

Then put nginx in front using `nginx.conf` (edit the domain and cert paths):

```bash
sudo apt-get install -y nginx
sudo cp nginx.conf /etc/nginx/sites-available/key-ai
sudo ln -s /etc/nginx/sites-available/key-ai /etc/nginx/sites-enabled/
sudo nginx -t && sudo systemctl reload nginx
```

## Manual run (dev)

```bash
source .venv/bin/activate
uvicorn main:app --host 0.0.0.0 --port 8000
```

## Test

```bash
curl http://127.0.0.1:8000/health
curl -X POST --data-binary @test.wav http://127.0.0.1:8000/audio
```

## Useful commands

```bash
systemctl status key-ai ollama
journalctl -u key-ai -f
sudo systemctl restart key-ai
ollama list
```

Notes: run a single Uvicorn worker (each loads its own Whisper). The API has no auth;
add an API-key check or restrict access in nginx before exposing it publicly.
