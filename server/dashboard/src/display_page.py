from pathlib import Path


# Three competition display views, switchable with ?variant=overview|ai|replay.
DISPLAY_PAGE = Path(__file__).with_name("display.html").read_text(encoding="utf-8")
