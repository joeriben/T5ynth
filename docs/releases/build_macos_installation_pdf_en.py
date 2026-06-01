from pathlib import Path

from PIL import Image
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.lib.utils import ImageReader
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
from reportlab.pdfgen import canvas


PAGE_W, PAGE_H = A4
MARGIN = 16 * mm
TEXT_W = PAGE_W - 2 * MARGIN

FONT_DIRS = [
    Path("/System/Library/Fonts/Supplemental"),
    Path("/Library/Fonts"),
]


def register_font():
    candidates = [
        ("Arial", "Arial"),
        ("Arial Unicode.ttf", "ArialUnicode"),
        ("Helvetica.ttc", "HelveticaSystem"),
    ]

    for directory in FONT_DIRS:
        for filename, name in candidates:
            path = directory / filename
            if path.exists():
                pdfmetrics.registerFont(TTFont(name, str(path)))
                return name

    return "Helvetica"


FONT_NAME = register_font()
FONT_BOLD = "Helvetica-Bold" if FONT_NAME == "Helvetica" else FONT_NAME


def draw_wrapped(c, text, x, y_top, font=FONT_NAME, size=12, leading=15):
    c.setFont(font, size)
    words = text.split()
    lines = []
    current = ""

    for word in words:
        test = word if not current else current + " " + word
        if c.stringWidth(test, font, size) <= TEXT_W:
            current = test
        else:
            if current:
                lines.append(current)
            current = word

    if current:
        lines.append(current)

    y = y_top
    for line in lines:
        c.drawString(x, y, line)
        y -= leading

    return y


def draw_bullets(c, items, x, y_top, size=12, leading=16):
    c.setFont(FONT_NAME, size)
    y = y_top
    for item in items:
        c.drawString(x, y, f"• {item}")
        y -= leading
    return y


def draw_image_page(c, title, caption, path):
    c.setFont(FONT_BOLD, 18)
    c.drawString(MARGIN, PAGE_H - MARGIN - 4, title)
    text_bottom = draw_wrapped(c, caption, MARGIN, PAGE_H - MARGIN - 28, size=11, leading=14)
    image_top = text_bottom - 10

    img = Image.open(path)
    iw, ih = img.size
    max_w = PAGE_W - 2 * MARGIN
    max_h = image_top - MARGIN
    scale = min(max_w / iw, max_h / ih, 0.5)
    draw_w = iw * scale
    draw_h = ih * scale
    x = (PAGE_W - draw_w) / 2
    y_img = image_top - draw_h

    c.drawImage(
        ImageReader(img),
        x,
        y_img,
        width=draw_w,
        height=draw_h,
        preserveAspectRatio=True,
        anchor="sw",
    )
    c.showPage()


def draw_text_page(c, title, paragraphs, bullets=None, note=None):
    c.setFont(FONT_BOLD, 18)
    c.drawString(MARGIN, PAGE_H - MARGIN - 4, title)
    y = PAGE_H - MARGIN - 28

    for paragraph in paragraphs:
        y = draw_wrapped(c, paragraph, MARGIN, y, size=11, leading=14)
        y -= 10

    if bullets:
        y = draw_bullets(c, bullets, MARGIN, y, size=12, leading=18)
        y -= 8

    if note:
        c.setFont(FONT_BOLD, 12)
        c.drawString(MARGIN, y, "Note")
        y -= 18
        draw_wrapped(c, note, MARGIN, y, size=11, leading=14)

    c.showPage()


def build_pdf():
    repo_root = Path("/Users/joerissen/ai/t5ynth")
    downloads = Path("/Users/joerissen/Downloads")
    out = repo_root / "docs/releases/T5ynth-macOS-Installation-EN.pdf"

    shots = [
        (
            "Step 1: Open the package",
            "If macOS blocks the package the first time, that is expected for this current build.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.07.png",
        ),
        (
            "Step 2: Open Privacy & Security",
            "In System Settings > Privacy & Security, scroll down and click “Open Anyway” for T5ynth.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.43.png",
        ),
        (
            "Step 3: Confirm opening",
            "Confirm the security prompt once more with “Open Anyway”.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.48.png",
        ),
        (
            "Step 4: The installer starts normally",
            "After that, the normal macOS installer opens.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.08.png",
        ),
        (
            "Step 5: License page",
            "Continue to the license page.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.17.png",
        ),
        (
            "Step 6: Accept the license",
            "Without “Accept”, the installation cannot continue.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.23.png",
        ),
        (
            "Step 7: Choose the install target",
            "In the standard case, choose installation for all users of this computer.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.33.png",
        ),
        (
            "Step 8: Start the installation",
            "Click “Installieren” to begin the standard installation on the system volume.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.40.png",
        ),
        (
            "Step 9: Confirm with password or Touch ID",
            "macOS asks for administrator permission for the actual installation.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.46.png",
        ),
        (
            "Step 10: Installation complete",
            "Once this dialog appears, T5ynth.app is correctly installed in /Applications. "
            "Then launch T5ynth and download a model in the Model Manager — every engine "
            "installs directly in the app (see the appendices).",
            downloads / "Bildschirmfoto 2026-04-18 um 10.15.04.png",
        ),
    ]

    ldm2_shots = [
        (
            "Appendix A1: Select AudioLDM2",
            "In the Model Manager, choose AudioLDM2. Like every other engine, T5ynth downloads this model directly from HuggingFace.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.05.png",
        ),
        (
            "Appendix A2: Confirm the license",
            "Before downloading, T5ynth shows the AudioLDM2 license terms. Click “Accept & Download” to continue.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.13.png",
        ),
        (
            "Appendix A3: Download in progress",
            "While the download is running, T5ynth shows the current file name and progress.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.21.png",
        ),
        (
            "Appendix A4: Backend starts",
            "After the download, the model is activated automatically and T5ynth briefly shows “Backend: Starting...”.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.36.12.png",
        ),
        (
            "Appendix A5: AudioLDM2 is ready",
            "Once “Installed” and “Backend: Connected” appear, AudioLDM2 is ready to use.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.36.19.png",
        ),
    ]

    c = canvas.Canvas(str(out), pagesize=A4)
    c.setTitle("T5ynth macOS Installation")

    c.setFont(FONT_BOLD, 22)
    c.drawString(MARGIN, PAGE_H - MARGIN - 10, "T5ynth macOS Installation")
    c.setFont(FONT_NAME, 13)

    y = PAGE_H - MARGIN - 40
    for paragraph in [
        "Short guide for the current unsigned macOS build with the usual Gatekeeper override step.",
        "The screenshots show the real installation flow on a Mac with a German user interface.",
        "Unfortunately I only had German screenshots available for this documentation pass, but the sequence is identical on English-language systems.",
        "Tip: every model loads directly in the app, with no HuggingFace account and no token – see the appendices. The recommended default model is Stable Audio 3 (Appendix C).",
    ]:
        y = draw_wrapped(c, paragraph, MARGIN, y, size=13, leading=17)
        y -= 10

    c.setFont(FONT_BOLD, 13)
    c.drawString(MARGIN, y - 4, "Quick summary")
    y -= 24
    draw_bullets(
        c,
        [
            "Open the .pkg",
            "If blocked: Privacy & Security > Open Anyway",
            "Click through the installer normally",
            "Launch T5ynth.app from /Applications",
        ],
        MARGIN,
        y,
    )
    c.showPage()

    for title, caption, path in shots:
        draw_image_page(c, title, caption, path)

    draw_text_page(
        c,
        "Appendix C: Loading Stable Audio 3 (recommended default)",
        [
            "Stable Audio 3 is T5ynth's current default model (version 2.1.0). There are two variants: Small Music for instrumental music and Small SFX for sound effects; both share one architecture and one t5gemma text encoder.",
            "Before downloading, T5ynth shows two licenses that both must be accepted: the Stability AI Community License for the audio model and the Google Gemma Terms of Use plus Prohibited Use Policy for the t5gemma encoder. The weights then download without a HuggingFace account or token.",
        ],
        bullets=[
            "Select “Stable Audio 3 Small Music” or “Small SFX” in the Model Manager.",
            "Accept both licenses with “Accept & Download”; the download starts.",
            "Both variants share the same t5gemma text encoder.",
            "Status switches to “Installed”, then “Backend: Connected”.",
        ],
        note="Stable Audio 3 reads up to 256 tokens per prompt. T5ynth automatically prepends the required modality prefix (“TrackType: Music, …” or “TrackType: SFX, …”). SA3 was trained on comma-joined, field-tagged metadata, so structured prompts such as “Instruments: synth pad, Moods: warm” fit its training distribution; plain descriptions work too.",
    )

    draw_text_page(
        c,
        "Appendix B: Loading Stable Audio Open 1.0",
        [
            "Stable Audio Open 1.0 is the original full-size Stable Audio Open checkpoint. It installs directly inside T5ynth, like Stable Audio 3.",
            "Before downloading, T5ynth shows the Stability AI Community License, which must be accepted. The ~4.85 GB checkpoint then downloads without a HuggingFace account or token, and a copy of the license is written into the model folder.",
        ],
        bullets=[
            "Select “Stable Audio Open 1.0” in the Model Manager.",
            "Accept the license with “Accept & Download”; the download starts.",
            "Status switches to “Installed”, then “Backend: Connected”.",
            "Already have the files from Stability by hand? Use “Auto-Scan” instead.",
        ],
        note="Stable Audio Open 1.0 needs the “T5-Base text encoder” (ungated, Apache-2.0). T5ynth installs it automatically together with the model – no extra step. Stable Audio Open 1.0 is also much larger than Small; for first tests on a new Mac, Stable Audio 3 or Stable Audio Open Small is usually the quicker starting point.",
    )

    draw_text_page(
        c,
        "Appendix D: Loading Stable Audio Open Small",
        [
            "Stable Audio Open Small is the compact, fastest Stable Audio Open checkpoint. It installs directly inside T5ynth, like Stable Audio 3 and Stable Audio Open 1.0 – no HuggingFace account and no manual download required any more.",
            "Before downloading, T5ynth shows the Stability AI Community License, which must be accepted. The ~1.68 GB checkpoint then downloads without a HuggingFace account or token – from an ungated mirror that redistributes the official weights unmodified – and a copy of the license is written into the model folder.",
        ],
        bullets=[
            "Select “Stable Audio Open Small” in the Model Manager.",
            "Accept the license with “Accept & Download”; the download starts.",
            "The required “T5-Base text encoder” (ungated, Apache-2.0) is installed automatically in the same step.",
            "Status switches to “Installed”, then “Backend: Connected”.",
            "Already have the files from Stability by hand? Use “Auto-Scan” instead.",
        ],
        note="Stable Audio Open Small is the fastest model and already converges at around 8 steps; higher step counts bring no benefit here. CFG should stay at 1.",
    )

    for title, caption, path in ldm2_shots:
        draw_image_page(c, title, caption, path)

    c.save()
    return out


if __name__ == "__main__":
    print(build_pdf())
