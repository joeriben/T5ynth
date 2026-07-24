"""Does the authoring TRACE actually arrive on the wire the plugin drives?

The LCO panel's trace (HEARD / LOOKED UP / WROTE / REPAIRED / RUNNING) is fed by
two fields backend/lco_write.py now reports and previously discarded:
`consultation` (which library entries the prompt's own words reached, and which
the author was shown for orientation anyway) and `repairs` (the Csound errors
the body had to be repaired past). If either is missing or mis-shaped, the panel
silently degrades to a trace with holes in it — visible only by eye, which is
exactly what this check exists to avoid.

So this exercises the ACTUAL wire (mode=csound over the pipe_inference IPC
subprocess, project rule), not an in-process call, and asserts the SHAPE the C++
side parses in PipeInference::authorCsoundOrchestra.

What it deliberately does NOT assert: which entries a given prompt reaches, or
that a body compiles first try. Those are the author's and the library's
business and change legitimately; the contract under test is that the trace
travels at all and is shaped as the panel reads it.

Run:  .venv/bin/python tools/lco_trace_wire_check.py
Writes tools/lco_trace_wire_out/results.json (untracked, like every *_out).
"""
import json, struct, subprocess, os, sys

REPO = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(REPO, "tools", "lco_trace_wire_out")
os.makedirs(OUT, exist_ok=True)


def read_exact(f, n):
    buf = b""
    while len(buf) < n:
        c = f.read(n - len(buf))
        if not c:
            raise RuntimeError("EOF")
        buf += c
    return buf


p = subprocess.Popen([os.path.join(REPO, ".venv/bin/python"), "-u",
                      os.path.join(REPO, "backend/pipe_inference.py")],
                     stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                     stderr=open(os.path.join(OUT, "stderr.log"), "wb"),
                     cwd=os.path.join(REPO, "backend"))
assert read_exact(p.stdout, 1) == b"\x02"
n = struct.unpack("<H", read_exact(p.stdout, 2))[0]
read_exact(p.stdout, n)
print("READY", flush=True)


def call(payload, partials=None):
    """One request. Interim \x04 frames (§4.6) are collected into `partials`
    before the frame that ends the request — reading the status byte only once
    would take the first of them for the answer."""
    p.stdin.write((json.dumps(payload, separators=(",", ":")) + "\n").encode())
    p.stdin.flush()
    while True:
        h = read_exact(p.stdout, 1)
        ln = struct.unpack("<I", read_exact(p.stdout, 4))[0]
        body = read_exact(p.stdout, ln).decode("utf-8", "replace")
        if h != b"\x04":
            break
        if partials is not None:
            partials.append(json.loads(body))
    if h == b"\x00":
        return {"ok": False, "error": "FRAME " + body}
    return json.loads(body)


# One prompt that reaches a library word ("string") and one that reaches almost
# nothing: the second is the case where `reached` is thin and `oriented_by`
# carries the starter top-up, which the panel MUST draw as a different thing.
CASES = ["a bowed cello", "the colour of wet slate at dusk"]

results, failures = [], []
for prompt in CASES:
    print(f"\n=== {prompt!r}", flush=True)
    partials = []
    r = call({"mode": "csound", "text": prompt, "stream": True}, partials)
    if not r.get("ok"):
        failures.append(f"{prompt!r}: authoring failed — {r.get('error')}")
        print("   FAILED:", r.get("error"), flush=True)
        continue

    c = r.get("consultation")
    if not isinstance(c, dict):
        failures.append(f"{prompt!r}: no `consultation` on the wire")
        continue
    reached = c.get("reached")
    if not isinstance(reached, dict):
        failures.append(f"{prompt!r}: `consultation.reached` is not an object")
        continue
    for section in ("instruments", "adjectives", "motions"):
        if not isinstance(reached.get(section), list):
            failures.append(f"{prompt!r}: `reached.{section}` is not a list")
    if not isinstance(c.get("oriented_by"), list):
        failures.append(f"{prompt!r}: `oriented_by` is not a list")
    if not isinstance(c.get("library_size"), int) or c.get("library_size") <= 0:
        failures.append(f"{prompt!r}: `library_size` is not a positive int")
    if not isinstance(r.get("repairs"), list):
        failures.append(f"{prompt!r}: `repairs` is not a list")
    if not isinstance(r.get("attempts"), int) or r.get("attempts") < 1:
        failures.append(f"{prompt!r}: `attempts` is not a positive int")
    if not isinstance(r.get("thinking"), str):
        failures.append(f"{prompt!r}: `thinking` is not a string")
    # The author is ASKED to reason in plain language before the fence, so an
    # empty thinking on every prompt means the extraction broke (or the author
    # stopped obeying HOW TO ANSWER) — either way the THOUGHT station goes dark
    # and nobody would notice by eye.
    elif not r["thinking"].strip():
        failures.append(f"{prompt!r}: the author wrote no reasoning outside the fence")
    # The thinking must be PROSE, not the code again: if the fence split went
    # wrong the body can land on both sides, and the panel would then quote
    # Csound as the machine's reasoning.
    elif r["thinking"].strip() == (r.get("params_text") or "").strip():
        failures.append(f"{prompt!r}: `thinking` is a copy of the body, not reasoning")

    # The reasoning has to arrive WHILE it is written, not only with the final
    # frame — that is the whole point of \x04. Three things can break silently
    # here and none of them shows by eye until someone watches a live authoring:
    # no frames at all (streaming off, or the gate not honoured), Csound in a
    # frame (the fence cut failed and the panel would print code as reasoning),
    # or a last frame that does not match the thinking the answer carries (the
    # panel would end on text the author revised away).
    if not partials:
        failures.append(f"{prompt!r}: nothing streamed — no \\x04 frame arrived")
    else:
        bad = [q for q in partials if q.get("kind") != "thinking"
               or not isinstance(q.get("text"), str)
               or not isinstance(q.get("attempt"), int)]
        if bad:
            failures.append(f"{prompt!r}: {len(bad)} interim frames are mis-shaped")
        leaked = [q for q in partials if "```" in q.get("text", "")]
        if leaked:
            failures.append(f"{prompt!r}: a fence reached the live reasoning "
                            f"({len(leaked)} frames) — the code would be shown as thought")
        last = partials[-1]
        if last.get("attempt") == r.get("attempts") and last.get("text") != (r.get("thinking") or ""):
            failures.append(f"{prompt!r}: the last streamed reasoning is not the "
                            "reasoning the answer carries")
        print(f"   streamed {len(partials)} frames, last from attempt "
              f"{last.get('attempt')} of {r.get('attempts')}", flush=True)

    # The one SEMANTIC invariant worth asserting: an entry may never be counted
    # both as something the prompt reached and as orientation it did not. That
    # distinction is the whole reason the panel draws the two differently, and a
    # regression in select() would make the trace quietly lie.
    overlap = set(c.get("oriented_by") or []) & set(reached.get("instruments") or [])
    if overlap:
        failures.append(f"{prompt!r}: {sorted(overlap)} counted as BOTH reached and orientation")

    n_reached = sum(len(reached.get(s) or []) for s in ("instruments", "adjectives", "motions"))
    print(f"   reached {n_reached} of {c.get('library_size')}: "
          f"{reached.get('instruments')} {reached.get('adjectives')} {reached.get('motions')}",
          flush=True)
    print(f"   orientation: {c.get('oriented_by')}", flush=True)
    print(f"   attempts {r.get('attempts')}, repairs {len(r.get('repairs') or [])}", flush=True)
    for e in (r.get("repairs") or []):
        print(f"      repaired past: {e}", flush=True)
    print(f"   author: {r.get('author_model')}", flush=True)
    print(f"   reading: {r.get('reading')}", flush=True)
    print("   thought:", flush=True)
    for ln in (r.get("thinking") or "").splitlines():
        print(f"      {ln}", flush=True)
    results.append({"prompt": prompt, "consultation": c,
                    "repairs": r.get("repairs"), "attempts": r.get("attempts"),
                    "reading": r.get("reading"), "thinking": r.get("thinking"),
                    "author_model": r.get("author_model")})

p.stdin.close()
p.wait(timeout=30)

with open(os.path.join(OUT, "results.json"), "w", encoding="utf-8") as fh:
    json.dump({"results": results, "failures": failures}, fh, indent=1)

print("\n" + ("FAIL\n" + "\n".join(failures) if failures else "PASS — the trace travels intact"),
      flush=True)
sys.exit(1 if failures else 0)
