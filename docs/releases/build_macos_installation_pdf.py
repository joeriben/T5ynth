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
        c.drawString(MARGIN, y, "Hinweis")
        y -= 18
        draw_wrapped(c, note, MARGIN, y, size=11, leading=14)

    c.showPage()


def build_pdf():
    repo_root = Path("/Users/joerissen/ai/t5ynth")
    downloads = Path("/Users/joerissen/Downloads")
    out = repo_root / "docs/releases/T5ynth-macOS-Installation-DE.pdf"

    shots = [
        (
            "Schritt 1: Paket per Doppelklick öffnen",
            "Wenn macOS das Paket beim ersten Versuch blockiert, ist das bei dieser Build-Version erwartbar.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.07.png",
        ),
        (
            "Schritt 2: In Datenschutz und Sicherheit gehen",
            "In Systemeinstellungen > Datenschutz und Sicherheit nach unten scrollen und bei T5ynth auf „Dennoch öffnen“ klicken.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.43.png",
        ),
        (
            "Schritt 3: Öffnen bestätigen",
            "Die Rückfrage noch einmal mit „Dennoch öffnen“ bestätigen.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.13.48.png",
        ),
        (
            "Schritt 4: Installer startet normal",
            "Danach öffnet sich der normale macOS-Installer.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.08.png",
        ),
        (
            "Schritt 5: Lizenzseite",
            "Zur Lizenzseite weitergehen.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.17.png",
        ),
        (
            "Schritt 6: Lizenz akzeptieren",
            "Ohne „Akzeptieren“ geht die Installation nicht weiter.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.23.png",
        ),
        (
            "Schritt 7: Installationsziel wählen",
            "Standardfall: „Für alle Benutzer:innen dieses Computers installieren“.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.33.png",
        ),
        (
            "Schritt 8: Installation starten",
            "Mit „Installieren“ die Standardinstallation auf dem Systemvolume starten.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.40.png",
        ),
        (
            "Schritt 9: Mit Passwort oder Touch ID bestätigen",
            "macOS fragt für die eigentliche Installation nach Administratorrechten.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.14.46.png",
        ),
        (
            "Schritt 10: Erfolgreiche Installation",
            "Wenn dieser Dialog erscheint, liegt T5ynth.app korrekt in /Applications. "
            "Danach T5ynth starten und im Model Manager ein Modell laden – alle Engines "
            "laden direkt in der App (siehe Anhänge).",
            downloads / "Bildschirmfoto 2026-04-18 um 10.15.04.png",
        ),
    ]

    model_manager_shot = (
        "Der Model Manager: Modelle in der App laden",
        "Alle Modelle werden direkt in der App geladen: Einstellungen > Model Manager. "
        "Jede Zeile zeigt ein Modell mit Name, benötigtem Text-Encoder (lädt automatisch "
        "mit), Statustext und einem Licht rechts – grün bedeutet vollständig installiert. "
        "Zum Laden das Modell auswählen, die Lizenz mit „Accept & Download“ bestätigen und "
        "den Fortschrittsbalken abwarten; danach wird das Licht grün, und „Backend: Connected“ "
        "am unteren Rand zeigt Einsatzbereitschaft. Dieser Screenshot zeigt den Zielzustand "
        "mit allen fünf Engines installiert.",
        downloads / "Bildschirmfoto 2026-06-01 um 23.05.37.png",
    )

    c = canvas.Canvas(str(out), pagesize=A4)
    c.setTitle("T5ynth macOS Installation")

    c.setFont(FONT_BOLD, 22)
    c.drawString(MARGIN, PAGE_H - MARGIN - 10, "T5ynth macOS Installation")
    c.setFont(FONT_NAME, 13)

    y = PAGE_H - MARGIN - 40
    for paragraph in [
        "Kurzanleitung für die aktuelle unsignierte macOS-Build mit dem üblichen Gatekeeper-Override.",
        "Die Screenshots zeigen den echten Installationsablauf auf einem Mac mit deutscher Systemoberfläche.",
        "Wichtig: Der zusätzliche Sicherheitsschritt ist nur einmalig nötig. Danach läuft die normale Installation über das .pkg.",
        "Tipp: Alle Modelle laden direkt in der App, ohne HuggingFace-Konto und ohne Token – siehe Anhänge. Empfohlenes Standard-Modell ist Stable Audio 3 (Anhang C).",
    ]:
        y = draw_wrapped(c, paragraph, MARGIN, y, size=13, leading=17)
        y -= 10

    c.setFont(FONT_BOLD, 13)
    c.drawString(MARGIN, y - 4, "Kurzfassung")
    y -= 24
    y = draw_bullets(
        c,
        [
            ".pkg öffnen",
            "Falls blockiert: Datenschutz und Sicherheit > Dennoch öffnen",
            "Installer normal durchklicken",
            "T5ynth.app aus /Applications starten",
        ],
        MARGIN,
        y,
    )

    c.showPage()

    for title, caption, path in shots:
        draw_image_page(c, title, caption, path)

    draw_image_page(c, *model_manager_shot)

    draw_text_page(
        c,
        "Anhang C: Stable Audio 3 laden (empfohlenes Standard-Modell)",
        [
            "Stable Audio 3 ist das aktuelle Standard-Modell von T5ynth (Version 2.1.0). Es gibt zwei Varianten: Small Music für Instrumentalmusik und Small SFX für Soundeffekte; beide nutzen dieselbe Architektur und denselben t5gemma-Text-Encoder.",
            "Vor dem Download zeigt T5ynth zwei Lizenzen an, die beide bestätigt werden müssen: die Stability AI Community License für das Audio-Modell und die Google Gemma Terms of Use samt Prohibited Use Policy für den t5gemma-Encoder. Die Gewichte werden anschließend ohne HuggingFace-Konto und ohne Token geladen.",
        ],
        bullets=[
            "Im Model Manager „Stable Audio 3 Small Music“ oder „Small SFX“ auswählen.",
            "Mit „Accept & Download“ beide Lizenzen bestätigen; der Download startet.",
            "Beide Varianten teilen sich denselben t5gemma-Text-Encoder.",
            "Status wechselt auf „Installed“, danach „Backend: Connected“.",
        ],
        note="Stable Audio 3 liest bis zu 256 Tokens pro Prompt. T5ynth stellt jedem Prompt automatisch das erforderliche Modality-Präfix („TrackType: Music, …“ bzw. „TrackType: SFX, …“) voran. SA3 wurde auf komma-getrennten, feldgetaggten Metadaten trainiert, daher passen strukturierte Prompts wie „Instruments: synth pad, Moods: warm“ gut zur Trainingsverteilung; freie Beschreibungen funktionieren ebenso.",
    )

    draw_text_page(
        c,
        "Anhang B: Stable Audio Open 1.0 laden",
        [
            "Stable Audio Open 1.0 ist der ursprüngliche Stable-Audio-Open-Checkpoint in voller Größe. Er installiert sich direkt in T5ynth, wie Stable Audio 3.",
            "Vor dem Download zeigt T5ynth die Stability AI Community License, die bestätigt werden muss. Der ~4,85 GB große Checkpoint wird anschließend ohne HuggingFace-Konto und ohne Token geladen, und eine Kopie der Lizenz wird in den Modellordner geschrieben.",
        ],
        bullets=[
            "Im Model Manager „Stable Audio Open 1.0“ auswählen.",
            "Mit „Accept & Download“ die Lizenz bestätigen; der Download startet.",
            "Status wechselt auf „Installed“, danach „Backend: Connected“.",
            "Dateien schon von Stability von Hand geladen? Stattdessen „Auto-Scan“ nutzen.",
        ],
        note="Stable Audio Open 1.0 benötigt zusätzlich den „T5-Base text encoder“ (ungated, Apache-2.0); dieser wird automatisch zusammen mit dem Modell geladen. Stable Audio Open 1.0 ist zudem deutlich größer als Small; für erste Tests auf einem neuen Mac sind deshalb meist Stable Audio 3 oder Stable Audio Open Small der schnellere Einstieg.",
    )

    draw_text_page(
        c,
        "Anhang D: Stable Audio Open Small laden",
        [
            "Stable Audio Open Small ist der kompakte, schnellste Stable-Audio-Open-Checkpoint. Er installiert sich direkt in T5ynth, wie Stable Audio 3 und Stable Audio Open 1.0 – kein HuggingFace-Konto und kein manueller Download mehr nötig.",
            "Vor dem Download zeigt T5ynth die Stability AI Community License, die bestätigt werden muss. Der ~1,68 GB große Checkpoint wird anschließend ohne HuggingFace-Konto und ohne Token geladen – aus einem ungated Spiegel, der die offiziellen Gewichte unverändert weiterverteilt – und eine Kopie der Lizenz wird in den Modellordner geschrieben.",
        ],
        bullets=[
            "Im Model Manager „Stable Audio Open Small“ auswählen.",
            "Mit „Accept & Download“ die Lizenz bestätigen; der Download startet.",
            "Der benötigte „T5-Base text encoder“ (ungated, Apache-2.0) wird im selben Schritt automatisch mitgeladen.",
            "Status wechselt auf „Installed“, danach „Backend: Connected“.",
            "Dateien schon von Stability von Hand geladen? Stattdessen „Auto-Scan“ nutzen.",
        ],
        note="Stable Audio Open Small ist das schnellste Modell und konvergiert bereits bei rund 8 Steps; höhere Step-Werte bringen hier nichts. CFG sollte bei 1 bleiben.",
    )

    draw_text_page(
        c,
        "Anhang A: AudioLDM2 laden",
        [
            "AudioLDM2 ist ein akademisches Latent-Diffusion-Text-zu-Audio-Modell (CVSSP / University of Surrey, Liu et al. 2023) für generelle Audio-, Musik- und Sprachsynthese. Es ist self-contained und braucht keinen separaten Text-Encoder.",
            "Vor dem Download zeigt T5ynth die Lizenz an, die bestätigt werden muss. Das Modell wird anschließend ungated direkt aus HuggingFace geladen – ohne Konto und ohne Token.",
        ],
        bullets=[
            "Im Model Manager „AudioLDM2“ auswählen.",
            "Mit „Accept & Download“ die Lizenz bestätigen; der Download startet.",
            "Status wechselt auf „Installed“, danach „Backend: Connected“.",
        ],
        note="Lizenz: CC BY-NC-SA 4.0 – nur nicht-kommerzielle Nutzung, keine Umsatzschwelle, keine Ausnahmen. Empfohlenes Standard-Modell bleibt Stable Audio 3 (Anhang C).",
    )

    c.save()
    return out


if __name__ == "__main__":
    print(build_pdf())
