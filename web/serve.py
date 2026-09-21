#!/usr/bin/env python3
"""A small webserver for browsing experiment results.

Serves the figures that the per-experiment plot scripts wrote:
  homepage         the experiments found under figures/ and results/
  /exp/<name>      one page per experiment, with a run and n picker

Generates nothing itself - it only shows what is already on disk.

    python3 web/serve.py [--port N] [--root DIR]
"""

import argparse
import glob
import json
import os
import re
import shutil
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer

WEB = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(WEB)

# Two figure layouts are supported, because the plot scripts have used both:
#   nested  figures/<exp>/<run>/n<N>/<kind>.png   (current)
#   flat    figures/<exp>/<run>/<kind>_n<N>.png   (earlier)
# Discovering rather than hardcoding is what lets the server survive the change.
BUCKET = re.compile(r"^n(?P<n>\d+)$")
OVERALL = "overall"  # figures/<exp>/<run>/overall/: the figures across n
FLAT = re.compile(r"^(?P<kind>.+)_n(?P<n>\d+)\.png$")
RUN = re.compile(r"^\d+$")
NUMBERED = re.compile(r"^exp(?P<number>\d+)$")

PAGES = {"index": "index.html", "experiment": "experiment.html"}
TYPES = {".html": "text/html; charset=utf-8",
         ".css": "text/css; charset=utf-8",
         ".js": "text/javascript; charset=utf-8",
         ".png": "image/png",
         ".csv": "text/csv; charset=utf-8"}


def subdirectories(path):
    try:
        names = os.listdir(path)
    except OSError:
        return []
    return [name for name in names if os.path.isdir(os.path.join(path, name))]


def experiments(root):
    """The experiment names, from figures/ and results/ together.

    An experiment that has been run but not yet plotted has a results/ directory
    and no figures/ one, and should still be listed.
    """
    names = set(subdirectories(os.path.join(root, "figures")))
    names |= set(subdirectories(os.path.join(root, "results")))
    return sorted(names)


def label(experiment):
    """The display name: exp1 is shown as "Experiment 1", anything else as is.

    Only the display changes - URLs and paths keep the directory name.
    """
    match = NUMBERED.match(experiment)
    return f"Experiment {int(match.group('number'))}" if match else experiment


def runs(root, experiment):
    """The run numbers for an experiment, newest first."""
    numbers = set()
    for top in ("figures", "results"):
        for name in subdirectories(os.path.join(root, top, experiment)):
            if RUN.match(name):
                numbers.add(int(name))
    return sorted(numbers, reverse=True)


def written(path):
    """Sort key: the order the plot script wrote the files in, then the name."""
    try:
        return (os.stat(path).st_mtime, os.path.basename(path))
    except OSError:
        return (0, os.path.basename(path))


def overall(root, experiment, run):
    """The run's figures across n, from figures/<exp>/<run>/overall/, in the
    order they were written."""
    directory = os.path.join(root, "figures", experiment, str(run), OVERALL)
    return [{"kind": os.path.splitext(os.path.basename(path))[0],
             "file": os.path.basename(path),
             "url": f"/figures/{experiment}/{run}/{OVERALL}/{os.path.basename(path)}"}
            for path in sorted(glob.glob(os.path.join(directory, "*.png")), key=written)]


def figures(root, experiment, run):
    """The figures of one run, as {n: [{"kind", "file", "url"}, ...]}.

    Reads both layouts. Within an n the figures keep the order the plot script
    wrote them in, which is the order it means them to be read - the headline
    figure first. Ordering by name instead would lead on whichever kind happens
    to sort first. Falls back to the name when the times are equal, so a fresh
    checkout still gets a stable order.
    """
    directory = os.path.join(root, "figures", experiment, str(run))
    found = {}

    # nested: a directory per n, one figure per kind inside it
    for bucket in sorted(subdirectories(directory)):
        match = BUCKET.match(bucket)
        if not match:
            continue
        n = int(match.group("n"))
        for path in sorted(glob.glob(os.path.join(directory, bucket, "*.png")), key=written):
            name = os.path.basename(path)
            found.setdefault(n, []).append({
                "kind": os.path.splitext(name)[0],
                "file": name,
                "url": f"/figures/{experiment}/{run}/{bucket}/{name}",
            })

    # flat: the n is part of the filename
    for path in sorted(glob.glob(os.path.join(directory, "*.png"))):
        name = os.path.basename(path)
        match = FLAT.match(name)
        if not match:
            continue
        n = int(match.group("n"))
        found.setdefault(n, []).append({
            "kind": match.group("kind"),
            "file": name,
            "url": f"/figures/{experiment}/{run}/{name}",
        })

    return found


def tables(root, experiment, run):
    """The CSVs of one run, as {n: {"file", "url"}}, for the download links."""
    directory = os.path.join(root, "results", experiment, str(run))
    found = {}
    for path in sorted(glob.glob(os.path.join(directory, "*.csv"))):
        name = os.path.basename(path)
        match = re.match(r"^.+_n(\d+)\.csv$", name)
        if not match:
            continue
        found[int(match.group(1))] = {
            "file": name,
            "url": f"/results/{experiment}/{run}/{name}",
        }
    return found


def summary(root):
    """The homepage payload: every experiment with its runs, the n values it has
    data for, and its figure count.

    The n values are taken from the CSVs as well as the figures, across every
    run, so a run that has been computed but not yet plotted still counts.
    """
    listing = []
    for name in experiments(root):
        numbers = runs(root, name)
        count = 0
        sizes = set()
        for run in numbers:
            found = figures(root, name, run)
            count += sum(len(images) for images in found.values())
            count += len(overall(root, name, run))
            sizes |= set(found) | set(tables(root, name, run))
        listing.append({"name": name, "label": label(name), "runs": numbers,
                        "sizes": sorted(sizes), "figure_count": count})
    return listing


def detail(root, experiment):
    """The experiment payload: every run's figures and CSVs, in one response.

    The whole tree is a few dozen files, so sending it at once keeps switching
    run or n instant with no further requests.
    """
    numbers = runs(root, experiment)
    return {
        "name": experiment,
        "label": label(experiment),
        "runs": numbers,
        "figures": {str(run): {str(n): images
                               for n, images in figures(root, experiment, run).items()}
                    for run in numbers},
        "overall": {str(run): overall(root, experiment, run) for run in numbers},
        "tables": {str(run): {str(n): table
                              for n, table in tables(root, experiment, run).items()}
                   for run in numbers},
    }


def safe(root, top, experiment, run, name, bucket=None):
    """Resolve a figure or result file, or None if anything does not check out.

    The path pieces from the URL are never joined blindly: the experiment must be
    one we discovered, the run must be digits, the optional bucket must look like
    n<N> or be overall/, and the file must actually sit in the resulting
    directory. The realpath check at the end is the backstop.
    """
    if experiment not in experiments(root):
        return None
    if not RUN.match(run):
        return None
    if bucket is not None and not (BUCKET.match(bucket) or bucket == OVERALL):
        return None
    if name != os.path.basename(name) or name.startswith("."):
        return None

    pieces = [root, top, experiment, run] + ([bucket] if bucket else [])
    directory = os.path.realpath(os.path.join(*pieces))
    path = os.path.realpath(os.path.join(directory, name))
    if os.path.dirname(path) != directory or not os.path.isfile(path):
        return None
    if os.path.commonpath([path, os.path.realpath(root)]) != os.path.realpath(root):
        return None
    return path


class Handler(BaseHTTPRequestHandler):
    server_version = "results"
    root = ROOT

    def log_message(self, format, *args):
        print(f"{self.address_string()} - {format % args}")

    # -- replies ----------------------------------------------------------

    def send_json(self, payload, status=200):
        body = json.dumps(payload).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json; charset=utf-8")
        self.send_header("Content-Length", str(len(body)))
        self.send_header("Cache-Control", "no-cache")
        self.end_headers()
        if self.command != "HEAD":
            self.wfile.write(body)

    def send_missing(self, message="not found"):
        self.send_json({"error": message}, status=404)

    def send_file(self, path, attachment=False):
        """Serve a file, streamed, with a no-cache validator.

        Figures are regenerated in place while the server runs, so the ETag is
        derived from mtime and size and revalidated on every request - a reload
        after re-plotting must show the new image, not a cached one.
        """
        try:
            info = os.stat(path)
        except OSError:
            return self.send_missing()

        tag = f'"{int(info.st_mtime)}-{info.st_size}"'
        if self.headers.get("If-None-Match") == tag:
            self.send_response(304)
            self.send_header("ETag", tag)
            self.send_header("Cache-Control", "no-cache")
            self.end_headers()
            return

        extension = os.path.splitext(path)[1].lower()
        self.send_response(200)
        self.send_header("Content-Type", TYPES.get(extension, "application/octet-stream"))
        self.send_header("Content-Length", str(info.st_size))
        self.send_header("ETag", tag)
        self.send_header("Cache-Control", "no-cache")
        if attachment:
            name = os.path.basename(path)
            self.send_header("Content-Disposition", f'attachment; filename="{name}"')
        self.end_headers()
        if self.command == "HEAD":
            return
        with open(path, "rb") as handle:
            shutil.copyfileobj(handle, self.wfile)

    def send_page(self, page):
        self.send_file(os.path.join(WEB, PAGES[page]))

    # -- routing ----------------------------------------------------------

    def do_HEAD(self):
        self.do_GET()

    def do_GET(self):
        path = self.path.split("?", 1)[0].split("#", 1)[0]
        parts = [part for part in path.split("/") if part]

        if not parts:
            return self.send_page("index")

        if parts[0] == "api":
            return self.api(parts[1:])

        if parts[0] == "exp" and len(parts) == 2:
            if parts[1] not in experiments(self.root):
                return self.send_missing(f"no experiment {parts[1]!r}")
            return self.send_page("experiment")

        # /figures/<exp>/<run>/<file>        flat layout
        # /figures/<exp>/<run>/n<N>/<file>   nested layout
        if parts[0] in ("figures", "results") and len(parts) in (4, 5):
            bucket = parts[3] if len(parts) == 5 else None
            target = safe(self.root, parts[0], parts[1], parts[2], parts[-1], bucket)
            if target is None:
                return self.send_missing()
            return self.send_file(target, attachment=parts[0] == "results")

        # Anything else is one of the static frontend files, by name only.
        if len(parts) == 1 and parts[0] in os.listdir(WEB):
            return self.send_file(os.path.join(WEB, parts[0]))

        return self.send_missing()

    def api(self, parts):
        if parts == ["experiments"]:
            return self.send_json(summary(self.root))
        if len(parts) == 2 and parts[0] == "experiments":
            if parts[1] not in experiments(self.root):
                return self.send_missing(f"no experiment {parts[1]!r}")
            return self.send_json(detail(self.root, parts[1]))
        return self.send_missing()


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--port", type=int, default=8000, help="port to listen on")
    parser.add_argument("--root", default=ROOT, help="repository root holding figures/ and results/")
    arguments = parser.parse_args()

    root = os.path.realpath(arguments.root)
    Handler.root = root

    # Flushed, so the summary still appears when stdout is a redirected log
    # rather than a terminal - a wrong --root should be obvious either way.
    found = summary(root)
    lines = [f"serving {root}"]
    if found:
        for entry in found:
            numbers = ", ".join(str(run) for run in entry["runs"]) or "none"
            lines.append(f"  {entry['label']}: runs {numbers} ({entry['figure_count']} figures)")
    else:
        lines.append("  no experiments found - is --root correct?")
    lines.append(f"http://localhost:{arguments.port}/")
    print("\n".join(lines), flush=True)

    server = ThreadingHTTPServer(("", arguments.port), Handler)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\nstopping")
        server.server_close()


if __name__ == "__main__":
    main()
