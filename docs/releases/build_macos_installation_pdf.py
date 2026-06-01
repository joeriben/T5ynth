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
            "Wenn dieser Dialog erscheint, liegt T5ynth.app korrekt in /Applications.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.15.04.png",
        ),
        (
            "Schritt 11: T5ynth öffnen und Modellstatus ansehen",
            "Im Model Manager ist Stable Audio Open Small zunächst noch nicht installiert.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.33.56.png",
        ),
        (
            "Schritt 12: Zugriff auf Downloads erlauben",
            "Wenn Auto-Scan die Modell-Dateien im Downloads-Ordner suchen soll, den Zugriff auf Downloads erlauben.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.34.10.png",
        ),
        (
            "Schritt 13: Modell-Dateien übernehmen",
            "T5ynth kopiert die Modell-Dateien aus Downloads in den internen Modellordner.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.34.16.png",
        ),
        (
            "Schritt 14: Backend startet",
            "Nach dem Kopieren erscheint zunächst der Übergangszustand „Backend: Starting...“.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.34.26.png",
        ),
        (
            "Schritt 15: Modell ist geladen",
            "Sobald „Installed“ und „Backend: Connected“ erscheinen, ist T5ynth einsatzbereit.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.34.39.png",
        ),
    ]

    ldm2_shots = [
        (
            "Anhang A1: AudioLDM2 auswählen",
            "Im Model Manager AudioLDM2 auswählen. Anders als Stable Audio Open Small kann T5ynth dieses Modell direkt aus HuggingFace herunterladen.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.05.png",
        ),
        (
            "Anhang A2: Lizenz bestätigen",
            "Vor dem Download zeigt T5ynth die Lizenzbedingungen für AudioLDM2 an. Mit „Accept & Download“ startet der Download.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.13.png",
        ),
        (
            "Anhang A3: Download läuft",
            "Während des Downloads zeigt T5ynth den aktuellen Dateinamen und den Fortschritt an.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.35.21.png",
        ),
        (
            "Anhang A4: Backend startet",
            "Nach dem Download wird das Modell automatisch aktiviert. Kurz darauf erscheint der Übergangszustand „Backend: Starting...“.",
            downloads / "Bildschirmfoto 2026-04-18 um 10.36.12.png",
        ),
        (
            "Anhang A5: AudioLDM2 ist bereit",
            "Sobald „Installed“ und „Backend: Connected“ erscheinen, ist AudioLDM2 einsatzbereit.",
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
        "Kurzanleitung für die aktuelle unsignierte macOS-Build mit dem üblichen Gatekeeper-Override.",
        "Die Screenshots zeigen den echten Installationsablauf auf einem Mac mit deutscher Systemoberfläche.",
        "Wichtig: Der zusätzliche Sicherheitsschritt ist nur einmalig nötig. Danach läuft die normale Installation über das .pkg.",
        "Tipp: Das empfohlene Standard-Modell Stable Audio 3 lädt direkt in der App – siehe Anhang C. Die Schritte 11–15 zeigen alternativ die manuelle Installation von Stable Audio Open Small.",
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
            "Stable Audio Open 1.0 ist in T5ynth als zusätzlicher Modellpfad vorhanden, wird aber derzeit nicht direkt in der App heruntergeladen.",
            "Der empfohlene Weg ist derselbe manuelle HuggingFace-Ablauf wie bei Stable Audio Open Small, nur mit dem Repository „stabilityai/stable-audio-open-1.0“.",
        ],
        bullets=[
            "Im Model Manager „Stable Audio Open 1.0“ auswählen.",
            "Mit „Open Model Page“ die HuggingFace-Seite öffnen.",
            "Dort die Lizenz akzeptieren und die Modell-Dateien aus dem Repository laden.",
            "Die Dateien in einen lokalen Ordner bringen und anschließend in T5ynth per „Browse…“ oder „Auto-Scan“ erfassen.",
            "Sobald T5ynth das Modell findet, sollte der Status auf „Installed“ und danach auf „Backend: Connected“ wechseln.",
        ],
        note="Stable Audio Open 1.0 ist deutlich größer als Stable Audio Open Small. Für erste Tests auf einem neuen Mac ist deshalb meist Stable Audio Open Small der sinnvollere Einstieg.",
    )

    for title, caption, path in ldm2_shots:
        draw_image_page(c, title, caption, path)

    c.save()
    return out


if __name__ == "__main__":
    print(build_pdf())
