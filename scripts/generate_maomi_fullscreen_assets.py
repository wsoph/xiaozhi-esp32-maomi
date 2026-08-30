#!/usr/bin/env python3
"""Generate the Maomi 240x240 orange, edge-to-edge, earless expression set."""

from pathlib import Path
from typing import Iterable

from PIL import Image, ImageDraw


SIZE = 240
SCALE = 3
ORANGE = "#FFB85C"
ORANGE_LIGHT = "#FFC873"
STRIPE = "#D97832"
INK = "#352D4A"
IRIS = "#59405E"
PINK = "#F49AB5"
CHEEK = "#F7BDD0"
WHITE = "#FFF9F6"
BLUE = "#63C9EE"
YELLOW = "#FFE072"
PURPLE = "#8D6BC1"
GREEN = "#79C98B"

STATIC_EXPRESSIONS = {
    "angry.png": ("angry", "frown"),
    "confident.png": ("confident", "smile"),
    "confused.png": ("confused", "o"),
    "cool.png": ("cool", "smile"),
    "crying.png": ("cry", "cry"),
    "delicious.png": ("closed", "tongue"),
    "embarrassed.png": ("embarrassed", "small"),
    "funny.png": ("wink", "tongue"),
    "happy.png": ("closed", "smile"),
    "kissy.png": ("closed", "kiss"),
    "laughing.png": ("closed", "laugh"),
    "loving.png": ("heart", "smile"),
    "neutral.png": ("open", "small"),
    "relaxed.png": ("closed", "smile"),
    "sad.png": ("sad", "frown"),
    "shocked.png": ("shocked", "o"),
    "silly.png": ("wink_right", "tongue"),
    "sleepy.png": ("sleepy", "small"),
    "surprised.png": ("open", "o"),
    "thinking.png": ("thinking", "small"),
    "winking.png": ("wink", "smile"),
}

ANIMATIONS = {
    "maomi_blink.gif": ("blink", 3, 140),
    "maomi_charge.gif": ("charge", 4, 420),
    "maomi_eat.gif": ("eat", 4, 260),
    "maomi_look.gif": ("look", 4, 420),
    "maomi_low_battery.gif": ("low_battery", 4, 360),
    "maomi_listening.gif": ("listening", 4, 360),
    "maomi_pet.gif": ("pet", 4, 300),
    "maomi_play.gif": ("play", 4, 300),
    "maomi_reminder.gif": ("reminder", 4, 220),
    "maomi_sleep.gif": ("sleep", 4, 700),
}


class Canvas:
    def __init__(self):
        self.image = Image.new("RGB", (SIZE * SCALE, SIZE * SCALE), ORANGE)
        self.draw = ImageDraw.Draw(self.image)

    @staticmethod
    def box(values: Iterable[float]):
        return tuple(round(value * SCALE) for value in values)

    def ellipse(self, values, fill, outline=None, width=1):
        self.draw.ellipse(self.box(values), fill=fill, outline=outline, width=width * SCALE)

    def line(self, values, fill, width=1, joint="curve"):
        self.draw.line(self.box(values), fill=fill, width=width * SCALE, joint=joint)

    def arc(self, values, start, end, fill, width=1):
        self.draw.arc(self.box(values), start=start, end=end, fill=fill, width=width * SCALE)

    def polygon(self, values, fill, outline=None):
        points = [(round(x * SCALE), round(y * SCALE)) for x, y in values]
        self.draw.polygon(points, fill=fill, outline=outline)

    def rounded_rectangle(self, values, radius, fill, outline=None, width=1):
        self.draw.rounded_rectangle(
            self.box(values), radius=radius * SCALE, fill=fill, outline=outline,
            width=width * SCALE,
        )

    def finish(self):
        return self.image.resize((SIZE, SIZE), Image.Resampling.LANCZOS)


def draw_base(canvas: Canvas, bob=0):
    # The orange fur reaches all four edges. The physical shell supplies the ears.
    canvas.rounded_rectangle((0, 0, 239, 239), 0, ORANGE)
    canvas.ellipse((8, 180 + bob, 232, 280 + bob), ORANGE_LIGHT)
    for x, tilt in ((84, -3), (105, -1), (128, 1), (151, 3)):
        canvas.line((x, 15 + bob, x + tilt, 48 + bob), STRIPE, 9)
    canvas.ellipse((25, 133 + bob, 85, 174 + bob), CHEEK)
    canvas.ellipse((155, 133 + bob, 215, 174 + bob), CHEEK)
    canvas.ellipse((42, 144 + bob, 66, 158 + bob), "#F8D8E3")
    canvas.ellipse((174, 144 + bob, 198, 158 + bob), "#F8D8E3")
    for y in (146, 157, 168):
        canvas.line((4, y + bob, 50, y - 2 + bob), WHITE, 3)
        canvas.line((190, y - 2 + bob, 236, y + bob), WHITE, 3)


def draw_open_eye(canvas: Canvas, cx, cy, pupil_dx=0, pupil_dy=0, scale=1.0):
    rx, ry = 24 * scale, 29 * scale
    canvas.ellipse((cx - rx, cy - ry, cx + rx, cy + ry), WHITE, INK, 4)
    canvas.ellipse((cx - 17 * scale, cy - 22 * scale,
                    cx + 17 * scale, cy + 22 * scale), IRIS)
    canvas.ellipse((cx - 8 * scale + pupil_dx, cy - 10 * scale + pupil_dy,
                    cx + 8 * scale + pupil_dx, cy + 10 * scale + pupil_dy), INK)
    canvas.ellipse((cx - 11 * scale + pupil_dx, cy - 16 * scale + pupil_dy,
                    cx - 3 * scale + pupil_dx, cy - 8 * scale + pupil_dy), WHITE)
    canvas.ellipse((cx + 7 * scale + pupil_dx, cy + 4 * scale + pupil_dy,
                    cx + 11 * scale + pupil_dx, cy + 8 * scale + pupil_dy), PINK)


def draw_eye(canvas: Canvas, cx, cy, style, side, motion=0):
    if style == "heart":
        canvas.ellipse((cx - 24, cy - 18, cx + 2, cy + 8), PINK, INK, 2)
        canvas.ellipse((cx - 2, cy - 18, cx + 24, cy + 8), PINK, INK, 2)
        canvas.polygon(((cx - 24, cy - 3), (cx + 24, cy - 3), (cx, cy + 25)), PINK)
        return
    if style == "cool":
        canvas.rounded_rectangle((cx - 31, cy - 18, cx + 31, cy + 17), 8, PURPLE, INK, 4)
        canvas.line((cx - 24, cy - 12, cx + 23, cy - 12), "#B9F3FF", 4)
        return
    if style in ("closed", "blink_closed"):
        canvas.arc((cx - 25, cy - 12, cx + 25, cy + 28), 200, 340, INK, 5)
        return
    if style == "sleepy":
        canvas.arc((cx - 24, cy - 20, cx + 24, cy + 18), 15, 165, INK, 5)
        canvas.arc((cx - 23, cy - 5, cx + 23, cy + 24), 195, 345, INK, 3)
        return
    if style == "sad":
        draw_open_eye(canvas, cx, cy + 5, scale=.85)
        canvas.line((cx - 22, cy - 29, cx + 17, cy - 35), INK, 5)
        return
    if style == "angry":
        draw_open_eye(canvas, cx, cy + 4, scale=.9)
        if side == "left":
            canvas.line((cx - 25, cy - 35, cx + 21, cy - 22), INK, 7)
        else:
            canvas.line((cx - 21, cy - 22, cx + 25, cy - 35), INK, 7)
        return
    if style == "confident":
        draw_open_eye(canvas, cx, cy + 3, scale=.86)
        canvas.line((cx - 24, cy - 25, cx + 23, cy - 32), INK, 5)
        return
    if style == "shocked":
        draw_open_eye(canvas, cx, cy + 3, scale=1.02)
        canvas.arc((cx - 28, cy - 47, cx + 28, cy - 12), 190, 350, INK, 4)
        return
    if style == "embarrassed":
        draw_open_eye(canvas, cx, cy + 4, scale=.82)
        return
    if style == "cry":
        canvas.arc((cx - 25, cy - 15, cx + 25, cy + 23), 20, 160, INK, 5)
        canvas.rounded_rectangle((cx - 10, cy + 5, cx + 10, 210), 8, BLUE)
        return
    if style == "thinking":
        draw_open_eye(canvas, cx, cy, pupil_dx=(-5 if side == "left" else -7), pupil_dy=-5)
        canvas.arc((cx - 25, cy - 46, cx + 25, cy - 10), 190, 345, INK, 4)
        return
    if style == "confused":
        draw_open_eye(canvas, cx, cy + 4, scale=.88)
        if side == "left":
            canvas.line((cx - 23, cy - 29, cx + 19, cy - 36), INK, 4)
        else:
            canvas.arc((cx - 23, cy - 46, cx + 23, cy - 14), 190, 350, INK, 4)
        return
    if style == "wink" and side == "left" or style == "wink_right" and side == "right":
        canvas.arc((cx - 25, cy - 12, cx + 25, cy + 28), 200, 340, INK, 5)
        return
    pupil_dx = motion if side == "left" else motion
    draw_open_eye(canvas, cx, cy, pupil_dx=pupil_dx)


def draw_mouth(canvas: Canvas, style, bob=0, motion=0):
    canvas.polygon(((114, 137 + bob), (126, 137 + bob), (120, 145 + bob)), PINK, WHITE)
    if style == "smile":
        canvas.arc((91, 137 + bob, 120, 174 + bob), 350, 95, INK, 4)
        canvas.arc((120, 137 + bob, 149, 174 + bob), 85, 190, INK, 4)
    elif style == "frown":
        canvas.arc((99, 154 + bob, 141, 184 + bob), 195, 345, INK, 5)
    elif style == "o":
        canvas.ellipse((107, 151 + bob, 133, 182 + bob), PINK, INK, 4)
    elif style == "laugh":
        canvas.ellipse((96, 146 + bob, 144, 196 + bob), INK)
        canvas.ellipse((108, 174 + bob, 132, 191 + bob), PINK)
        canvas.line((103, 155 + bob, 137, 155 + bob), WHITE, 4)
    elif style == "tongue":
        canvas.arc((99, 142 + bob, 141, 177 + bob), 5, 175, INK, 4)
        canvas.rounded_rectangle((112, 160 + bob, 128, 190 + bob + motion), 8, PINK, INK, 3)
    elif style == "kiss":
        canvas.ellipse((112, 154 + bob, 128, 166 + bob), PINK, INK, 3)
        canvas.polygon(((154, 142), (166, 130), (178, 142), (166, 157)), PINK)
    elif style == "cry":
        canvas.ellipse((101, 148 + bob, 139, 190 + bob), INK)
        canvas.ellipse((111, 171 + bob, 129, 185 + bob), PINK)
    else:
        canvas.arc((103, 144 + bob, 120, 165 + bob), 340, 100, INK, 4)
        canvas.arc((120, 144 + bob, 137, 165 + bob), 80, 200, INK, 4)


def draw_expression(eye_style, mouth_style, *, motion=0, bob=0, accessory=None):
    canvas = Canvas()
    draw_base(canvas, bob)
    style = "cool" if eye_style == "cool" else eye_style
    draw_eye(canvas, 74, 101 + bob, style, "left", motion)
    draw_eye(canvas, 166, 101 + bob, style, "right", motion)
    draw_mouth(canvas, mouth_style, bob, motion)

    if eye_style == "embarrassed":
        for x in (38, 48, 58, 182, 192, 202):
            canvas.line((x, 139, x + 6, 151), PINK, 3)
    if accessory == "charge":
        canvas.rounded_rectangle((88, 190, 152, 220), 5, WHITE, INK, 4)
        canvas.rounded_rectangle((152, 199, 158, 211), 2, INK)
        fill = 99 + motion * 12
        canvas.rounded_rectangle((94, 196, min(fill, 146), 214), 3, GREEN)
        canvas.polygon(((121, 192), (110, 207), (121, 207), (114, 221),
                        (135, 201), (123, 201)), YELLOW, INK)
    elif accessory == "eat":
        x = 165 - motion * 7
        canvas.ellipse((x, 180, x + 43, 205), YELLOW, INK, 3)
        canvas.polygon(((x + 43, 192), (x + 57, 181), (x + 57, 204)), YELLOW, INK)
    elif accessory == "low_battery":
        canvas.rounded_rectangle((89, 190, 151, 218), 5, WHITE, INK, 4)
        canvas.rounded_rectangle((151, 198, 157, 210), 2, INK)
        canvas.rounded_rectangle((95, 196, 104 + motion * 2, 212), 2, "#E95C66")
    elif accessory == "pet":
        radius = 12 + motion * 2
        cx, cy = 120, 205
        canvas.ellipse((cx - radius - 10, cy - radius, cx + 2, cy + radius), PINK)
        canvas.ellipse((cx - 2, cy - radius, cx + radius + 10, cy + radius), PINK)
        canvas.polygon(((cx - radius - 10, cy), (cx + radius + 10, cy), (cx, cy + 28)), PINK)
    elif accessory == "play":
        x = 67 + motion * 28
        canvas.ellipse((x, 184, x + 34, 218), PURPLE, INK, 3)
        canvas.arc((x + 7, 190, x + 27, 211), 30, 220, YELLOW, 4)
        canvas.line((x + 28, 188, x + 46, 172), INK, 3)
    elif accessory == "reminder":
        shake = (-1, 1, -1, 1)[motion]
        canvas.ellipse((91 + shake, 182, 149 + shake, 224), YELLOW, INK, 4)
        canvas.arc((86 + shake, 171, 154 + shake, 215), 195, 345, INK, 5)
        canvas.line((120 + shake, 178, 120 + shake, 205), INK, 4)
        canvas.line((120 + shake, 205, 136 + shake, 211), INK, 4)
    elif accessory == "sleep":
        offset = motion * 3
        canvas.line((167 + offset, 67 - offset, 190 + offset, 67 - offset), PURPLE, 5)
        canvas.line((190 + offset, 67 - offset, 168 + offset, 91 - offset), PURPLE, 5)
        canvas.line((168 + offset, 91 - offset, 193 + offset, 91 - offset), PURPLE, 5)
        canvas.line((193 + offset, 39 - offset, 208 + offset, 39 - offset), PURPLE, 4)
        canvas.line((208 + offset, 39 - offset, 194 + offset, 54 - offset), PURPLE, 4)
        canvas.line((194 + offset, 54 - offset, 211 + offset, 54 - offset), PURPLE, 4)
    return canvas.finish()


def draw_listening(index):
    canvas = Canvas()
    draw_base(canvas)
    draw_open_eye(canvas, 74, 100, pupil_dy=-3)
    draw_open_eye(canvas, 166, 100, pupil_dy=-3)
    draw_mouth(canvas, "small")

    wave_count = (1, 2, 3, 2)[index]
    left_waves = ((16, 70, 49, 132), (19, 78, 41, 124), (25, 86, 33, 116))
    right_waves = ((191, 70, 224, 132), (199, 78, 221, 124), (207, 86, 215, 116))
    for wave in range(wave_count):
        canvas.arc(left_waves[wave], 270, 90, BLUE, 4)
        canvas.arc(right_waves[wave], 90, 270, BLUE, 4)
    return canvas.finish()


def animation_frame(kind, index):
    if kind == "blink":
        return draw_expression("blink_closed" if index == 1 else "open", "small")
    if kind == "charge":
        return draw_expression("closed", "smile", motion=index, accessory="charge")
    if kind == "eat":
        return draw_expression("closed", "o" if index % 2 else "smile",
                               motion=index, accessory="eat")
    if kind == "look":
        return draw_expression("open", "small", motion=(-8, 0, 8, 0)[index])
    if kind == "low_battery":
        return draw_expression("sad", "frown", motion=index, accessory="low_battery")
    if kind == "listening":
        return draw_listening(index)
    if kind == "pet":
        return draw_expression("closed", "smile", motion=(0, 2, 3, 2)[index], accessory="pet")
    if kind == "play":
        return draw_expression("open", "smile", motion=index, accessory="play")
    if kind == "reminder":
        return draw_expression("shocked", "o", motion=index, accessory="reminder")
    if kind == "sleep":
        return draw_expression("closed", "small", bob=(0, 2, 4, 2)[index],
                               motion=index, accessory="sleep")
    raise ValueError(kind)


def save_gif(path, frames, duration):
    paletted = [frame.quantize(colors=128, method=Image.Quantize.MEDIANCUT) for frame in frames]
    paletted[0].save(
        path, save_all=True, append_images=paletted[1:], duration=duration,
        loop=0, optimize=False, disposal=2,
    )


def main():
    output = Path(__file__).resolve().parents[1] / "main" / "boards" / "zhengchen" / \
        "1.54tft-wifi-maomi" / "assets-extra"
    output.mkdir(parents=True, exist_ok=True)
    for filename, (eyes, mouth) in STATIC_EXPRESSIONS.items():
        accessory = "thinking" if filename == "thinking.png" else None
        draw_expression(eyes, mouth, accessory=accessory).save(output / filename, optimize=True)
    for filename, (kind, frame_count, duration) in ANIMATIONS.items():
        frames = [animation_frame(kind, index) for index in range(frame_count)]
        save_gif(output / filename, frames, duration)
    print(f"Generated {len(STATIC_EXPRESSIONS)} PNGs and {len(ANIMATIONS)} GIFs in {output}")


if __name__ == "__main__":
    main()
