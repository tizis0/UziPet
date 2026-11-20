import json
import math
from pathlib import Path
import random
import shutil
import subprocess
import threading
import time
import winsound

import uzipet


FOLDER = Path(__file__).resolve().parent
PHRASES = json.loads((FOLDER / "phrases.json").read_text(encoding="utf-8"))
UI = PHRASES["ui"]
BUTTONS = [
    {"id": key, "label": label}
    for key, label in zip(("polite", "rude", "cancel"), PHRASES["choices"])
]


class Timer:
    def __init__(self):
        self.lock = threading.RLock()
        self.tier = 1
        self.seconds = 300
        self.deadline = None
        self.ringing = False
        self.worker = None
        self.generation = 0
        self.audio = None
        self.form_open = False

    def phrase(self, kind):
        return random.choice(PHRASES[kind][self.tier])

    def status(self):
        if self.deadline is None:
            return ""
        remaining = max(0, math.ceil(self.deadline - time.monotonic()))
        return UI["remaining"].format(remaining // 3600, remaining // 60 % 60, remaining % 60)

    def form(self, kind="prompt", values=None):
        hours, rest = divmod(self.seconds, 3600)
        minutes, seconds = divmod(rest, 60)
        if values is None:
            values = dict(hours=str(hours), minutes=str(minutes), seconds=str(seconds))
        fields = [
            {"id": key, "label": UI[key], "value": values[key], "type": "integer", "length": 2}
            for key in ("hours", "minutes", "seconds")
        ]
        uzipet.show_dialog("setup", self.phrase(kind), BUTTONS, fields, status=self.status(), cancel="cancel")
        self.form_open = True

    def acknowledge(self):
        uzipet.show_dialog("signal", self.phrase("ring"), [{"id": "calm", "label": UI["calm"]}])

    def schedule(self, delay, callback):
        self.worker = threading.Timer(delay, callback, args=(self.generation,))
        self.worker.daemon = True
        self.worker.start()

    def start(self, seconds, rude):
        if self.worker is not None:
            self.worker.cancel()
        self.generation += 1
        self.seconds = seconds
        extra = random.randint(15, 120) if rude else 0
        duration = seconds + extra
        self.deadline = time.monotonic() + duration
        self.schedule(min(duration, 1), self.countdown)
        uzipet.say(self.phrase("rude" if rude else "polite"))

    def play_wav(self):
        try:
            winsound.PlaySound(str(FOLDER / "timer.wav"),
                               winsound.SND_FILENAME | winsound.SND_ASYNC | winsound.SND_LOOP)
        except RuntimeError:
            pass

    def play(self):
        player = shutil.which("ffplay.exe")
        if player:
            for name in ("timer.mp3", "timer.ogg"):
                track = FOLDER / name
                if track.is_file():
                    try:
                        self.audio = subprocess.Popen(
                            [player, "-nodisp", "-loglevel", "error", "-loop", "0", str(track)],
                            stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL, creationflags=subprocess.CREATE_NO_WINDOW)
                        return
                    except OSError:
                        break
        self.play_wav()

    def countdown(self, generation):
        with self.lock:
            if generation != self.generation or self.deadline is None:
                return
            left = self.deadline - time.monotonic()
            if left > 0:
                if self.form_open:
                    uzipet.update_dialog("setup", status=self.status())
                self.schedule(min(left, 1), self.countdown)
                return
            self.deadline = None
            self.ringing = True
            self.form_open = False
            self.play()
            uzipet.close_dialog("setup")
            uzipet.capture_clicks(True)
            uzipet.pace(2.2, across_monitors=True)
            uzipet.say(self.phrase("ring"), interrupt=True)
            self.schedule(7, self.remind)

    def remind(self, generation):
        with self.lock:
            if generation != self.generation or not self.ringing:
                return
            if self.audio is not None and self.audio.poll() is not None:
                self.audio = None
                self.play_wav()
            uzipet.say(self.phrase("ring"))
            self.schedule(7, self.remind)

    def calm(self):
        self.ringing = False
        self.generation += 1
        if self.worker is not None:
            self.worker.cancel()
        if self.audio is not None:
            self.audio.terminate()
            self.audio = None
        winsound.PlaySound(None, 0)
        uzipet.pace()
        uzipet.capture_clicks(False)
        uzipet.say(self.phrase("calm"))

    def submit(self, event):
        if event["id"] == "signal":
            if self.ringing and event["button"] == "calm":
                self.calm()
            return
        if event["id"] != "setup":
            return
        self.form_open = False
        if self.ringing:
            return
        button = event["button"]
        if button == "cancel":
            uzipet.say(self.phrase("cancel"))
        elif button in ("polite", "rude"):
            values = event["fields"]
            hours, minutes, seconds = (int(values[key] or "0") for key in ("hours", "minutes", "seconds"))
            duration = hours * 3600 + minutes * 60 + seconds
            if minutes > 59 or seconds > 59 or duration == 0:
                self.form("invalid", values)
            else:
                self.start(duration, button == "rude")

    def event(self, event):
        with self.lock:
            kind = event["type"]
            if kind == "start":
                uzipet.pet_menu("open", UI["menu"], icon="clock.svg")
            elif kind == "relationship":
                self.tier = event["tier"]
            elif kind == "pet_menu" and event["id"] == "open":
                if self.ringing:
                    self.acknowledge()
                else:
                    self.form()
            elif kind == "click" and self.ringing:
                self.acknowledge()
            elif kind == "dialog":
                self.submit(event)


on_event = Timer().event
