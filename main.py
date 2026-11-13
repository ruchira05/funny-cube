"""Small voice-AI API server: raw audio in -> {text, reply, emotion} JSON out.

ESP32 --POST raw audio--> FastAPI --> faster-whisper (STT) --> Ollama (short reply + emotion)
"""
import asyncio
import hashlib
import hmac
import io
import logging
import os
import re
import time
import wave
from collections import OrderedDict
from contextlib import asynccontextmanager
from pathlib import Path

import httpx
import numpy as np
from faster_whisper import WhisperModel
from fastapi import Depends, FastAPI, Header, HTTPException, Request

log = logging.getLogger("key-ai")
logging.basicConfig(level=logging.INFO)

# Minimal .env loader (real environment variables win).
_env = Path(__file__).parent / ".env"
if _env.exists():
    for _line in _env.read_text().splitlines():
        if "=" in _line and not _line.lstrip().startswith("#"):
            _k, _v = _line.split("=", 1)
            os.environ.setdefault(_k.strip(), _v.strip())

# SHA-256 hex digest of the API key. Create one with: python gen_key.py
API_KEY_HASH = os.getenv("API_KEY_HASH", "").strip().lower()

WHISPER_MODEL = os.getenv("WHISPER_MODEL", "tiny.en")  # or base.en
WHISPER_COMPUTE = os.getenv("WHISPER_COMPUTE", "int8")
OLLAMA_URL = os.getenv("OLLAMA_URL", "http://127.0.0.1:11434")
LLM_MODEL = os.getenv("LLM_MODEL", "llama3.2:3b")  # any Ollama model, e.g. phi3:mini
MAX_TOKENS = int(os.getenv("MAX_TOKENS", "30"))  # ~12 words + emotion tag
MAX_AUDIO_BYTES = int(os.getenv("MAX_AUDIO_BYTES", str(5 * 1024 * 1024)))
DEFAULT_SAMPLE_RATE = int(os.getenv("SAMPLE_RATE", "16000"))  # for headerless PCM

EMOTIONS = ["happy", "sad", "angry", "surprised", "neutral", "thinking", "love"]

NUM_CTX = int(os.getenv("NUM_CTX", "1024"))  # small context = less memory, faster
HISTORY_TURNS = int(os.getenv("HISTORY_TURNS", "4"))  # user+assistant pairs remembered
HISTORY_TTL = int(os.getenv("HISTORY_TTL", "600"))  # seconds idle before a conversation resets
MAX_SESSIONS = int(os.getenv("MAX_SESSIONS", "5"))

# Compact output ("emotion|reply") needs ~9 fewer tokens than JSON.
SYSTEM_PROMPT = (
    "Friendly cute human-like assistant. Output only: emotion|reply\n"
    f"emotion: {', '.join(EMOTIONS)}, matching your reply. "
    "reply: casual, warm, one plain spoken sentence, around 15 words. "
    "Never invent facts or names; if unsure, say so.\n"
    "Example: happy|Glad to hear it!"
)
_FORMAT = re.compile(r"^\W*(?:emotion\W*)?([a-z]+)\W*\|\s*(?:reply\W*)?(.+)$", re.S | re.I)
# Model sometimes drops the "|": "Neutral I'm fine" / "happy: Glad to help"
_LEAD = re.compile(rf"^\W*({'|'.join(EMOTIONS)})\b(\s*[:,.\-–]\s*)?(.*)$", re.S | re.I)

state: dict = {}
# In-memory conversation history: session id -> {"t": last_used, "msgs": [...]}. Lost on restart.
histories: OrderedDict[str, dict] = OrderedDict()


def _get_history(sid: str) -> list[dict]:
    h = histories.get(sid)
    if not h or time.monotonic() - h["t"] > HISTORY_TTL:
        histories.pop(sid, None)
        return []
    return list(h["msgs"])


def _save_history(sid: str, user: str, assistant: str) -> None:
    msgs = (_get_history(sid) + [{"role": "user", "content": user}, {"role": "assistant", "content": assistant}])
    histories[sid] = {"t": time.monotonic(), "msgs": msgs[-2 * HISTORY_TURNS:]}
    histories.move_to_end(sid)
    while len(histories) > MAX_SESSIONS:  # evict least recently used
        histories.popitem(last=False)


async def require_api_key(x_api_key: str | None = Header(default=None)):
    """Hash the presented key and compare it to the stored hash in constant time."""
    presented = hashlib.sha256((x_api_key or "").encode()).hexdigest()
    if not x_api_key or not hmac.compare_digest(presented, API_KEY_HASH):
        raise HTTPException(401, "invalid or missing API key", headers={"WWW-Authenticate": "ApiKey"})


@asynccontextmanager
async def lifespan(app: FastAPI):
    if len(API_KEY_HASH) != 64:  # fail closed: never run unauthenticated
        raise RuntimeError("API_KEY_HASH not set (or not a SHA-256 hex digest). Run: python gen_key.py")
    log.info("Loading Whisper model %s (%s)...", WHISPER_MODEL, WHISPER_COMPUTE)
    state["whisper"] = WhisperModel(WHISPER_MODEL, device="cpu", compute_type=WHISPER_COMPUTE)
    state["http"] = httpx.AsyncClient(base_url=OLLAMA_URL, timeout=60)
    await asyncio.to_thread(_transcribe, np.zeros(16000, dtype=np.float32))  # warm Whisper
    warm = asyncio.create_task(_warm_llm())  # don't block startup: /health answers immediately
    yield
    warm.cancel()
    await state["http"].aclose()


async def _warm_llm():
    """Load the model into memory (and pin it there). Retries while Ollama is still booting."""
    for attempt in range(1, 41):
        try:
            await _llm("hi")
            log.info("LLM warm: model loaded and pinned in memory")
            return
        except Exception as e:  # noqa: BLE001
            log.warning("LLM warm-up attempt %d failed (%s: %s); retrying", attempt, type(e).__name__, e)
            await asyncio.sleep(3)
    log.error("LLM warm-up gave up; the first request will load the model")


app = FastAPI(title="key-ai", lifespan=lifespan)


def _decode_audio(data: bytes, sample_rate: int) -> np.ndarray:
    """Return mono float32 audio at 16 kHz from a WAV file or raw 16-bit PCM."""
    if data[:4] == b"RIFF":
        try:
            with wave.open(io.BytesIO(data)) as w:
                sr, ch, width = w.getframerate(), w.getnchannels(), w.getsampwidth()
                if width != 2:
                    raise ValueError("only 16-bit WAV supported")
                pcm = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16)
        except (wave.Error, ValueError) as e:
            raise HTTPException(400, f"bad WAV: {e}")
        if ch > 1:
            pcm = pcm.reshape(-1, ch).mean(axis=1).astype(np.int16)
    else:
        sr = sample_rate
        pcm = np.frombuffer(data[: len(data) // 2 * 2], dtype=np.int16)

    audio = pcm.astype(np.float32) / 32768.0
    if sr != 16000 and len(audio):  # cheap linear resample
        n = int(len(audio) * 16000 / sr)
        audio = np.interp(np.linspace(0, len(audio) - 1, n), np.arange(len(audio)), audio).astype(np.float32)
    return audio


def _transcribe(audio: np.ndarray) -> str:
    segments, _ = state["whisper"].transcribe(
        audio, language="en", beam_size=1, vad_filter=True, condition_on_previous_text=False
    )
    return " ".join(s.text.strip() for s in segments).strip()


async def _llm(text: str, history: list[dict] | None = None) -> dict:
    r = await state["http"].post(
        "/api/chat",
        json={
            "model": LLM_MODEL,
            "stream": False,
            "keep_alive": -1,  # never unload: avoids a ~17 s reload after idle
            "options": {"num_predict": MAX_TOKENS, "temperature": 0.7, "num_ctx": NUM_CTX},
            "messages": [
                {"role": "system", "content": SYSTEM_PROMPT},
                *(history or []),
                {"role": "user", "content": text},
            ],
        },
    )
    r.raise_for_status()
    body = r.json()
    ns = lambda k: body.get(k, 0) / 1e9  # noqa: E731
    log.info(
        "ollama: load=%.2fs prompt_eval=%.2fs (%d tok) generate=%.2fs (%d tok, %.1f tok/s)",
        ns("load_duration"), ns("prompt_eval_duration"), body.get("prompt_eval_count", 0),
        ns("eval_duration"), body.get("eval_count", 0),
        body.get("eval_count", 0) / max(ns("eval_duration"), 1e-9),
    )
    raw = body["message"]["content"].strip()
    m = _FORMAT.match(raw)
    if m and m.group(1).lower() in EMOTIONS:
        emotion, reply = m.group(1).lower(), m.group(2)
    elif (lead := _LEAD.match(raw)) and lead.group(3).strip():  # emotion word but no "|"
        emotion = lead.group(1).lower()
        # strip the word only when it is clearly a tag; "Happy to help!" stays intact
        reply = lead.group(3) if (lead.group(2) or emotion == "neutral") else raw
    else:  # model ignored the format: use the whole text as the reply
        emotion, reply = "neutral", raw.replace("|", " ")
    reply = " ".join(reply.split()).strip(" \"'")
    return {"reply": reply or "Sorry, I didn't catch that.", "emotion": emotion}


@app.get("/health")
async def health():
    return {"ok": True, "whisper": WHISPER_MODEL, "llm": LLM_MODEL}


def _session_id(device_id: str | None) -> str:
    return re.sub(r"[^\w.-]", "", device_id or "")[:64] or "default"


@app.post("/reset", dependencies=[Depends(require_api_key)])
async def reset(x_device_id: str | None = Header(default=None)):
    """Forget the conversation for this device (optional X-Device-Id header)."""
    histories.pop(_session_id(x_device_id), None)
    return {"ok": True}


@app.post("/audio", dependencies=[Depends(require_api_key)])
async def audio_endpoint(
    request: Request, sample_rate: int = DEFAULT_SAMPLE_RATE, x_device_id: str | None = Header(default=None)
):
    """Body = raw audio bytes (WAV, or headerless 16-bit mono PCM at ?sample_rate=).

    Optional header X-Device-Id keeps a separate conversation per device (default: one shared)."""
    data = await request.body()
    if not data:
        raise HTTPException(400, "empty body")
    if len(data) > MAX_AUDIO_BYTES:
        raise HTTPException(413, "audio too large")

    t0 = time.perf_counter()
    audio = _decode_audio(data, sample_rate)
    if len(audio) < 1600:  # < 0.1 s
        raise HTTPException(400, "audio too short")
    t1 = time.perf_counter()

    text = await asyncio.to_thread(_transcribe, audio)  # keep event loop free
    t2 = time.perf_counter()
    if not text:
        return {"text": "", "reply": "I didn't hear anything.", "emotion": "neutral"}

    sid = _session_id(x_device_id)
    try:
        result = await _llm(text, _get_history(sid))
    except httpx.HTTPError as e:
        log.error("LLM error: %s", e)
        raise HTTPException(502, "LLM backend unavailable")
    _save_history(sid, text, f"{result['emotion']}|{result['reply']}")

    t3 = time.perf_counter()
    log.info(
        "timing: audio=%.1fs decode=%.3fs stt=%.2fs llm=%.2fs total=%.2fs",
        len(audio) / 16000, t1 - t0, t2 - t1, t3 - t2, t3 - t0,
    )
    return {"text": text, **result}
