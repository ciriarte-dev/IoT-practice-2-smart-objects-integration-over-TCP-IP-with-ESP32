"""Clasificación y vigencia de una única lectura, independiente de TCP."""


class DistanceController:
    OFF = "LED ALL OFF"

    def __init__(self, ttl=2.0):
        self.ttl = ttl
        self.command = self.OFF
        self.valid_until = None

    def invalidate(self):
        self.command = self.OFF
        self.valid_until = None

    def update(self, distance, now):
        if distance is None:
            self.invalidate()
            return
        self.valid_until = now + self.ttl
        if distance < 20:
            self.command = "LED RED BLINK"
        elif distance <= 40:
            self.command = "LED YELLOW ON"
        else:
            self.command = "LED GREEN ON"

    def expire(self, now):
        if self.valid_until is not None and now >= self.valid_until:
            self.invalidate()
            return True
        return False
