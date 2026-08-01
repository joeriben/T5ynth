#!/usr/bin/env python3
"""Version control for BJ's approvals: an approval is for a SOUND, not for a key.

BJ, 2026-07-31, after finding approval claimed in several places at once and
contradicting itself inside the same entry: „ES WIRD NUN EINE ZENTRALE STELLE
DAFÜR EINGEFÜHRT, die MANDATORY ist für das Panel. Ground truth." — and
immediately after: „inkl. versionsverwaltung".

The central place is the entry's `heard` block in `backend/dco_lexicon.json`,
and `tools/lco_build_library.py` is what makes it mandatory: it COMPOSES the
library's `curated` string from `heard` and refuses any hand-written second
opinion. This tool adds the half that was missing — the version.

WHY A VERSION.  „abgenommen" is a verdict on the instrument he actually heard.
Nothing stopped the body being rewritten afterwards while the verdict stayed
behind, still naming the entry, now describing a sound nobody had listened to.
So every approval records `heard.version`: a hash over the fields that make the
SOUND — `code`, `params`, `anchor_code`. Change one and the recorded version
stops matching; the build tool then withholds the entry and it leaves the panel
by itself, until BJ has heard the new version and it is restamped.

AND A NUMBER HE CAN SAY OUT LOUD.  BJ, 2026-08-02, approving `string`: „ich
hatte angeordnet eine versionsnummer zu vergeben, das wäre dann 0.8 oder so."
The hash answers "is this still the sound he heard"; it cannot answer "how far
along is this instrument", and that was half of what „inkl. versionsverwaltung"
asked for. `heard.versionsnummer` is that half: a number BJ names when he
approves, so an entry can be approved AND openly unfinished — `string` is
approved at 0.8 with its damp axis known to saturate above C5. It is his to
give, never derived, so `--restamp` requires it in the same breath as his words.

Prose does NOT invalidate an approval: rewriting `why`, adding a surface form
or correcting a listening note changes nothing about what he heard.

    python3 tools/lco_approvals.py --check          # approved / stale / unversioned
    python3 tools/lco_approvals.py --restamp KEY --by "<BJ's own words>"

`--restamp` is the only writer, and it refuses to run without his words. It
cannot create an approval either: an entry whose `heard.status` is not
„abgenommen" is not something a tool can stamp into existence.
"""

import argparse
import hashlib
import json
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
LEXICON = ROOT / "backend" / "dco_lexicon.json"

# The fields that make the sound. Everything else in an entry is prose about it.
SOUND_FIELDS = ("code", "params", "anchor_code")

APPROVED = "abgenommen"


def load():
    return json.loads(LEXICON.read_text(encoding="utf-8"))


def save(lex):
    LEXICON.write_text(json.dumps(lex, indent=1, ensure_ascii=False) + "\n",
                       encoding="utf-8")


def version_of(entry):
    """The entry's sound, as a short stable hash.

    sort_keys so a re-serialisation in a different key order is not mistaken for
    a changed instrument; only SOUND_FIELDS go in, so editing a `why` or a
    listening note cannot retire an approval BJ already gave.
    """
    payload = {k: entry.get(k) for k in SOUND_FIELDS}
    blob = json.dumps(payload, sort_keys=True, ensure_ascii=False).encode("utf-8")
    return hashlib.sha256(blob).hexdigest()[:16]


def state_of(entry):
    """(state, detail) for one entry's approval, from `heard` alone."""
    h = entry.get("heard") or {}
    if h.get("status") != APPROVED:
        return None, ""
    recorded = h.get("version")
    current = version_of(entry)
    if not recorded:
        return "UNVERSIONED", (f"approved but not bound to a version; current sound "
                               f"is {current}")
    if recorded != current:
        return "STALE", (f"approved on {recorded}, current sound is {current} — the "
                         f"body changed after he heard it")
    number = h.get("versionsnummer")
    if not number:
        # Reported, not failed. The four approvals that predate BJ's order carry no
        # number and inventing one for them would be exactly the thing this file
        # exists to stop: a version nobody said out loud.
        return "OK (no number)", (f"{h.get('datum', '?')}  {current}  — approved before "
                                  f"the version number existed; BJ has not named one")
    return "OK", f"v{number}  {h.get('datum', '?')}  {current}"


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--check", action="store_true",
                    help="verify every approval against the entry's current sound")
    ap.add_argument("--restamp", metavar="KEY",
                    help="bind KEY's approval to its CURRENT sound (only after BJ heard it)")
    ap.add_argument("--by", help="BJ's own words approving this version — required with --restamp")
    ap.add_argument("--version-number", metavar="N",
                    help="the version number BJ named for this sound, e.g. 0.8 — "
                         "required with --restamp; never derived, only quoted")
    args = ap.parse_args()

    lex = load()
    entries = {e["key"]: e for e in lex["techniques"]}

    if args.restamp:
        key = args.restamp
        entry = entries.get(key)
        if entry is None:
            sys.exit(f"no lexicon entry '{key}'")
        h = entry.get("heard") or {}
        if h.get("status") != APPROVED:
            sys.exit(f"'{key}' is not approved (heard.status = {h.get('status')!r}). "
                     f"A tool does not approve an instrument; only BJ does.")
        if not args.by:
            sys.exit("--restamp needs --by with BJ's own words about THIS version.")
        if not args.version_number:
            sys.exit("--restamp needs --version-number with the number BJ named. It is "
                     "his to give and this tool does not derive one.")
        h["version"] = version_of(entry)
        h["version_bj"] = args.by
        h["versionsnummer"] = args.version_number
        entry["heard"] = h
        save(lex)
        print(f"{key}: v{h['versionsnummer']}, bound to {h['version']}")
        return 0

    rows = [(k, *state_of(e)) for k, e in entries.items()]
    rows = [r for r in rows if r[1] is not None]
    # A missing version number is a gap in the RECORD, not in the sound: those three
    # were approved before BJ ordered a number and only he can name one. It prints and
    # does not fail, or every CI run would go red on something no tool may repair.
    bad = [r for r in rows if r[1] not in ("OK", "OK (no number)")]
    unnumbered = [r for r in rows if r[1] == "OK (no number)"]
    for key, state, detail in sorted(rows, key=lambda r: (r[1] != "OK", r[0])):
        print(f"  {state:14} {key:16} {detail}")
    print(f"\n{len(rows) - len(bad)} approved and current, {len(bad)} needing attention"
          + (f"; {len(unnumbered)} carry no version number yet" if unnumbered else ""))
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
