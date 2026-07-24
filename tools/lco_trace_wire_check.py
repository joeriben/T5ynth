"""Does the authoring TRACE actually arrive on the wire the plugin drives?

The LCO panel's trace (HEARD / OPENED / WROTE / REPAIRED / RUNNING) is fed by
two fields backend/lco_write.py now reports and previously discarded:
`consultation` (which library entries the AUTHOR asked to have opened, having
read the index and decided for itself) and `repairs` (the Csound errors the body
had to be repaired past). If either is missing or mis-shaped, the panel silently
degrades to a trace with holes in it — visible only by eye, which is exactly
what this check exists to avoid.

So this exercises the ACTUAL wire (mode=csound over the pipe_inference IPC
subprocess, project rule), not an in-process call, and asserts the SHAPE the C++
side parses in PipeInference::authorCsoundOrchestra.

What it deliberately does NOT assert: which entries a given prompt opens, or
that a body compiles first try. Those are the author's own decisions and change
legitimately — asserting them here would be this check telling the model what to
choose. The contract under test is that the trace travels at all and is shaped
as the panel reads it.

Run:  .venv/bin/python tools/lco_trace_wire_check.py
Writes tools/lco_trace_wire_out/results.json (untracked, like every *_out).
"""
import json, re, struct, subprocess, os, sys

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


# One prompt whose vocabulary the library plainly covers and one whose does not:
# the second is where the author has nothing obvious to reach for, and either
# names entries anyway or names none — the branch where the whole library is
# opened. Both must arrive on the wire in the same shape.
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
    opened, named = c.get("opened"), c.get("named")
    if not isinstance(opened, dict) or not isinstance(named, dict):
        failures.append(f"{prompt!r}: `consultation.opened`/`named` is not an object")
        continue
    for section in ("instruments", "adjectives", "motions"):
        if not isinstance(opened.get(section), list):
            failures.append(f"{prompt!r}: `opened.{section}` is not a list")
        if not isinstance(named.get(section), list):
            failures.append(f"{prompt!r}: `named.{section}` is not a list")
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

    # The authoring has to arrive WHILE it is written, not only with the final
    # frame — that is the whole point of \x04. Things that break silently here
    # show by eye only when someone watches a live authoring: no frames at all
    # (streaming off, or the gate not honoured), Csound in a thinking frame
    # (the fence cut failed and the panel would print code as reasoning), a
    # last frame that does not match what the answer carries (the panel would
    # end on text the author revised away), or no body frames (the panel goes
    # dark for the code — the long half of the generation, which is what §4.6's
    # `body` kind exists for).
    if not partials:
        failures.append(f"{prompt!r}: nothing streamed — no \\x04 frame arrived")
    else:
        # Per-kind shape, per §4.6. A kind this check does not know is what a
        # CLIENT must ignore; a checker that ignored it too would let a typo'd
        # kind ship as a frame every client drops on the floor.
        def misshaped(q):
            k = q.get("kind")
            if k in ("thinking", "body"):
                return not isinstance(q.get("text"), str) or not isinstance(q.get("attempt"), int)
            if k == "attempt":
                return (not isinstance(q.get("attempt"), int)
                        or not isinstance(q.get("max"), int)
                        or not isinstance(q.get("errors"), list))
            return True
        bad = [q for q in partials if misshaped(q)]
        if bad:
            failures.append(f"{prompt!r}: {len(bad)} interim frames are mis-shaped")
        thoughts = [q for q in partials if q.get("kind") == "thinking"]
        bodies   = [q for q in partials if q.get("kind") == "body"]
        leaked = [q for q in thoughts + bodies if "```" in q.get("text", "")]
        if leaked:
            failures.append(f"{prompt!r}: a fence marker reached the live stream "
                            f"({len(leaked)} frames)")
        if thoughts:
            last = thoughts[-1]
            if last.get("attempt") == r.get("attempts") and last.get("text") != (r.get("thinking") or ""):
                failures.append(f"{prompt!r}: the last streamed reasoning is not the "
                                "reasoning the answer carries")
        if not bodies:
            failures.append(f"{prompt!r}: the code never streamed — no `body` frame")
        else:
            # The live code the panel ended on must be the code the card then
            # carries: every line of the final body has to have streamed. Not
            # byte equality — sanitize APPENDS one line nothing could have
            # streamed, the `asig = <var>` recovery for a body that routed its
            # output through `out` under another name. Only that trailing line
            # is exempt, and only in that exact shape: excluding every `asig =`
            # line would hide a real one that failed to stream.
            streamed = {ln.strip() for ln in bodies[-1].get("text", "").splitlines() if ln.strip()}
            final = [ln.strip() for ln in (r.get("params_text") or "").splitlines() if ln.strip()]
            if final and re.fullmatch(r"asig\s*=\s*\w+", final[-1]) and final[-1] not in streamed:
                final = final[:-1]
            missing = [ln for ln in final if ln not in streamed]
            if missing:
                failures.append(f"{prompt!r}: {len(missing)} of {len(final)} body lines "
                                f"never streamed (first: {missing[0]!r})")
        print(f"   streamed {len(partials)} frames ({len(thoughts)} thinking, "
              f"{len(bodies)} body), attempts {r.get('attempts')}", flush=True)

    # The SEMANTIC invariants worth asserting. `named` is read out of the
    # author's own prose, so a broken reader would happily report entries that
    # were never fetched — and the panel would show the machine consulting things
    # it never saw. `named` must therefore be a SUBSET of `opened`: everything
    # asked for was handed over, and the two differ only by what the backend
    # added on top (the whole library, when no instrument was recognised).
    sections = ("instruments", "adjectives", "motions")
    n_opened = sum(len(opened.get(s) or []) for s in sections)
    n_named = sum(len(named.get(s) or []) for s in sections)
    if n_opened > c["library_size"]:
        failures.append(f"{prompt!r}: {n_opened} entries opened out of a library of "
                        f"{c['library_size']}")
    if len(set(sum((opened.get(s) or [] for s in sections), []))) != n_opened:
        failures.append(f"{prompt!r}: the same entry is listed twice in `opened`")
    for s in sections:
        extra = set(named.get(s) or []) - set(opened.get(s) or [])
        if extra:
            failures.append(f"{prompt!r}: {sorted(extra)} named in {s} but never opened")
    # The panel reads a full `opened` as "the whole library was opened"; that is
    # only ever true when no instrument was named, and it must stay true.
    if n_opened >= c["library_size"] and (named.get("instruments") or []):
        failures.append(f"{prompt!r}: the whole library was opened although "
                        f"{named['instruments']} was named")

    print(f"   named {n_named}, opened {n_opened} of {c.get('library_size')}: "
          f"{named.get('instruments')} {named.get('adjectives')} {named.get('motions')}",
          flush=True)
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
