#!/usr/bin/env bash
# Ubuntu setup: system deps, Ollama, model pull, Python venv, systemd services.
# Usage: ./setup.sh            (run as a normal user with sudo access)
#        LLM_MODEL=phi3:mini ./setup.sh
set -euo pipefail

APP_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP_USER="${SUDO_USER:-$USER}"
LLM_MODEL="${LLM_MODEL:-llama3.2:3b}"
WHISPER_MODEL="${WHISPER_MODEL:-tiny.en}"

echo "==> Installing system packages"
sudo apt-get update -y
sudo apt-get install -y python3 python3-venv python3-pip curl ffmpeg

echo "==> Installing Ollama (official Linux installer, sets up the 'ollama' systemd service)"
if ! command -v ollama >/dev/null; then
  curl -fsSL https://ollama.com/install.sh | sh
fi
sudo systemctl enable --now ollama

echo "==> Waiting for Ollama API"
for _ in $(seq 1 30); do
  curl -sf http://127.0.0.1:11434/api/version >/dev/null && break
  sleep 1
done

echo "==> Pulling model: $LLM_MODEL"
ollama pull "$LLM_MODEL"

echo "==> Creating Python venv"
python3 -m venv "$APP_DIR/.venv"
"$APP_DIR/.venv/bin/pip" install --upgrade pip
"$APP_DIR/.venv/bin/pip" install -r "$APP_DIR/requirements.txt"

if ! grep -qs '^API_KEY_HASH=' "$APP_DIR/.env"; then
  echo "==> Generating API key (shown once, save it!)"
  "$APP_DIR/.venv/bin/python" "$APP_DIR/gen_key.py"
fi

echo "==> Pre-downloading Whisper model: $WHISPER_MODEL"
"$APP_DIR/.venv/bin/python" -c "from faster_whisper import WhisperModel; WhisperModel('$WHISPER_MODEL', device='cpu', compute_type='int8')"

echo "==> Installing key-ai systemd service"
sudo tee /etc/systemd/system/key-ai.service >/dev/null <<EOF
[Unit]
Description=key-ai voice API
After=network.target ollama.service
Wants=ollama.service

[Service]
User=$APP_USER
WorkingDirectory=$APP_DIR
EnvironmentFile=$APP_DIR/.env
Environment=LLM_MODEL=$LLM_MODEL
Environment=WHISPER_MODEL=$WHISPER_MODEL
ExecStart=$APP_DIR/.venv/bin/uvicorn main:app --host 127.0.0.1 --port 8000
Restart=always
RestartSec=3

[Install]
WantedBy=multi-user.target
EOF
sudo systemctl daemon-reload
sudo systemctl enable --now key-ai

echo
echo "Done. Check:  curl http://127.0.0.1:8000/health"
echo "Logs:         journalctl -u key-ai -f"
echo "Nginx:        use nginx.conf (proxies 443 -> 127.0.0.1:8000)"
