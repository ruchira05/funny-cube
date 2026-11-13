"""Generate an API key. Prints the key ONCE and stores only its SHA-256 hash in .env.

Usage: python gen_key.py [--force]
"""
import hashlib
import os
import secrets
import sys
from pathlib import Path

ENV = Path(__file__).parent / ".env"

if ENV.exists() and "API_KEY_HASH=" in ENV.read_text() and "--force" not in sys.argv:
    sys.exit(".env already has API_KEY_HASH. Use --force to rotate the key (old key stops working).")

key = "kai_" + secrets.token_urlsafe(32)
key_hash = hashlib.sha256(key.encode()).hexdigest()

lines = [l for l in (ENV.read_text().splitlines() if ENV.exists() else []) if not l.startswith("API_KEY_HASH=")]
lines.append(f"API_KEY_HASH={key_hash}")
ENV.write_text("\n".join(lines) + "\n")
os.chmod(ENV, 0o600)

print("Your API key (save it now, it is not stored anywhere):\n")
print(f"  {key}\n")
print("Send it on every request:  X-API-Key: <key>")
print(f"Hash saved to {ENV}")
