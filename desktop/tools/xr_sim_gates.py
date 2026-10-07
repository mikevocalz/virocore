#!/usr/bin/env python3
"""Acceptance gates for viro_sim_host against Meta XR Simulator.

Runs the gates from docs/META_XR_SIMULATOR_VULKAN.md on a live simulator,
once per device profile and texture transport, and exits non-zero on any
failure. Needs MetaXRSimulator running, the Meta XR Operator layer installed
(metavr CLI), a built desktop/build/viro_sim_host, and Pillow.

    python3 desktop/tools/xr_sim_gates.py [--profiles "Meta VR Glasses,Meta Quest 3"]
                                          [--transports gpu_handle,jpg]
                                          [--skip-modes] [--skip-hello-xr]

Per profile it runs: the render and input gates for each transport; the
immersive, passthrough and passthrough-fallback display modes; and Khronos
hello_xr (Metal) on the same runtime, built from the OpenXR source CMake
already fetched into desktop/build/_deps.

Two simulator traps this script handles:
  * The config env var is META_XRSIM_CONFIG_JSON. Any other name is ignored
    silently and the simulator runs on its bundled defaults.
  * device_profile in persistent_data.json beats the config file. The script
    writes the profile there for each run and restores the file at the end.
"""
import argparse, base64, io, json, os, re, shutil, subprocess, sys, tempfile, time

from PIL import Image

HERE = os.path.dirname(os.path.abspath(__file__))
HOST = os.path.join(HERE, "..", "build", "viro_sim_host")
OPENXR_SRC = os.path.join(HERE, "..", "build", "_deps", "openxr-src")
HELLO_BUILD = os.path.join(HERE, "..", "build", "hello_xr")
HELLO = os.path.join(HELLO_BUILD, "src", "tests", "hello_xr", "hello_xr")
SIM = "/Applications/MetaXRSimulator.app/Contents/Resources/MetaXRSimulator"
OPERATOR = os.path.expanduser(
    "~/Library/Application Support/metavr/tools/meta-xr-operator/"
    "meta-xr-operator-standalone-public/macos")
PERSIST = os.path.expanduser(
    "~/Library/Application Support/MetaXR/MetaXrSimulator/persistent_data.json")

failures = []


def check(name, ok, detail=""):
    print(f"  [{'PASS' if ok else 'FAIL'}] {name}{'  ' + detail if detail else ''}")
    if not ok:
        failures.append(name)


class Operator:
    """Minimal stdio MCP client for the Meta XR Operator proxy."""

    def __init__(self):
        self.p = subprocess.Popen([os.path.join(OPERATOR, "meta-xr-operator-mcp-proxy")],
                                  stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                  stderr=subprocess.DEVNULL, text=True)
        self.id = 0
        self.rpc("initialize", {"protocolVersion": "2024-11-05", "capabilities": {},
                                "clientInfo": {"name": "xr_sim_gates", "version": "1"}})
        self.send({"jsonrpc": "2.0", "method": "notifications/initialized"})

    def send(self, msg):
        self.p.stdin.write(json.dumps(msg) + "\n")
        self.p.stdin.flush()

    def rpc(self, method, params):
        self.id += 1
        self.send({"jsonrpc": "2.0", "id": self.id, "method": method, "params": params})
        for line in self.p.stdout:
            try:
                msg = json.loads(line)
            except ValueError:
                continue
            if msg.get("id") == self.id:
                return msg.get("result", {})
        raise RuntimeError(f"operator proxy closed during {method}")

    def call(self, tool, **args):
        return self.rpc("tools/call", {"name": tool, "arguments": args}).get("content", [])

    def capture(self, eye):
        for c in self.call("openxr_capture_composited_image", eye=eye):
            if c.get("type") == "image":
                return Image.open(io.BytesIO(base64.b64decode(c["data"]))).convert("RGB")
        return None

    def close(self):
        self.p.stdin.close()
        self.p.terminate()


def lit(img):
    """Pixel count, centroid x and bbox of non-black pixels."""
    w, h = img.size
    xs = []
    for i, (r, g, b) in enumerate(img.getdata()):
        if r + g + b > 60:
            xs.append(i % w)
    return len(xs), (sum(xs) / len(xs) if xs else None)


def wait_for(log, pattern, timeout=20):
    end = time.time() + timeout
    while time.time() < end:
        if re.search(pattern, open(log).read(), re.M):
            return True
        time.sleep(0.5)
    return False


def set_profile(profile):
    data = json.load(open(PERSIST)) if os.path.exists(PERSIST) else {}
    data["device_profile"] = profile
    json.dump(data, open(PERSIST, "w"), indent=2)


def launch(profile, transport, workdir, tag, extra_env=None):
    """Starts viro_sim_host on the given profile; returns (process, log path)."""
    cfg = json.load(open(os.path.join(HERE, "sim-config.json")))
    cfg["device_profile"], cfg["ses_texture_format"] = profile, transport
    cfg_path = os.path.join(workdir, "sim-config.json")
    json.dump(cfg, open(cfg_path, "w"))
    set_profile(profile)

    log = os.path.join(workdir, f"host-{profile.replace(' ', '_')}-{tag}.log")
    env = dict(os.environ,
               XR_RUNTIME_JSON=os.path.join(SIM, "meta_openxr_simulator.json"),
               META_XRSIM_CONFIG_JSON=cfg_path,
               XR_API_LAYER_PATH=OPERATOR,
               XR_ENABLE_API_LAYERS="XR_APILAYER_METAX_operator",
               VIRO_FRAMES="216000", **(extra_env or {}))
    host = subprocess.Popen([HOST], cwd=os.path.dirname(HOST), env=env,
                            stdout=open(log, "w"), stderr=subprocess.STDOUT)
    return host, log


def stop(host):
    host.terminate()
    host.wait(timeout=10)
    time.sleep(2)


def run(profile, transport, op_ok, workdir):
    print(f"\n### {profile} / ses_texture_format={transport}")
    host, log = launch(profile, transport, workdir, transport)
    try:
        if not wait_for(log, r"^session state -> 5"):
            check("session reaches FOCUSED", False, f"see {log}")
            return
        time.sleep(2.5)
        text = open(log).read()

        sysname = re.search(r"^system: (.*)$", text, re.M)
        check("profile applied", sysname and sysname.group(1) == profile,
              sysname.group(1) if sysname else "no system line")
        check("no GL/GLES extension requested",
              not re.search(r"opengl", "\n".join(l for l in text.splitlines()
                                                 if not l.startswith("[Meta XR Simulator]")), re.I))
        check("XR_KHR_metal_enable", "ext XR_KHR_metal_enable: yes" in text)
        binds = re.findall(r"^input-xr: (.*?) -> (-?\d+)$", text, re.M)
        check("input bindings + attach succeed", binds and all(r == "0" for _, r in binds),
              ", ".join(f"{n}={r}" for n, r in binds if r != "0"))
        px = re.search(r"^eye 0 readback .* lit=(\d+)/(\d+)", text, re.M)
        check("Gate A: swapchain readback non-black", px and int(px.group(1)) > 1000,
              f"lit {px.group(1)}/{px.group(2)}" if px else "no readback line")
        has_gaze = "eye gaze not supported" not in text
        if not op_ok:
            return

        op = Operator()
        try:
            eyes = {e: op.capture(e) for e in ("left", "right")}
            stats = {e: lit(img) if img else (0, None) for e, img in eyes.items()}
            for e, img in eyes.items():
                if img:
                    img.save(os.path.join(workdir, f"{profile.replace(' ', '_')}-{transport}-{e}.png"))
            # The box carries an orange/white checker; an untextured material
            # renders one flat color and fails this.
            left = eyes["left"]
            if left:
                px = list(left.getdata())
                orange = sum(1 for r, g, b in px if r > 150 and 60 < g < 160 and b < 90)
                white = sum(1 for r, g, b in px if r > 170 and g > 170 and b > 170)
                check("diffuse texture sampled (checker visible)", orange > 2000 and white > 2000,
                      f"orange={orange} white={white}")
            check("Gate C: both eyes non-black",
                  all(n > 1000 for n, _ in stats.values()),
                  " ".join(f"{e}={n}" for e, (n, _) in stats.items()))
            (_, lx), (_, rx) = stats["left"], stats["right"]
            check("stereo disparity (left centroid right of right's)",
                  lx is not None and rx is not None and lx > rx, f"L={lx} R={rx}")

            op.call("openxr_set_head_pose", position=[0, 1.7, 0],
                    orientation=[0, 0.1736, 0, 0.9848])
            time.sleep(1)
            yawed = op.capture("left")
            op.call("openxr_set_head_pose", position=[0, 1.7, 0], orientation=[0, 0, 0, 1])
            yx = lit(yawed)[1] if yawed else None
            check("head yaw left moves scene right", yx is not None and lx is not None and yx > lx + 50,
                  f"x {lx} -> {yx}")

            # Aim the right controller straight ahead from eye height. The
            # box sits on that ray, so the trigger must click it even on
            # devices without eye gaze.
            op.call("openxr_set_controller_pose", hand="right", pose_type="aim",
                    position=[0.0, 0.0, 0.0], orientation=[0, 0, 0, 1], base_space="view")
            time.sleep(1)
            mark = len(open(log).read())
            op.call("openxr_set_controller_input", hand="right", component="Trigger", value=1.0)
            time.sleep(1)
            op.call("openxr_set_controller_input", hand="right", component="Trigger", value=0.0)
            time.sleep(1)
            check("controller aim + trigger hits the box",
                  re.search(r"PINCH DOWN .*hit=node", open(log).read()[mark:]) is not None)

            op.call("openxr_release_input_devices")
            if has_gaze:
                time.sleep(2)
                mark = len(open(log).read())
                op.call("openxr_gaze_and_pinch", gaze_orientation=[0, 0, 0, 1], hand="right",
                        hold_seconds=0.6)
                time.sleep(3)
                check("gaze+pinch hits the box",
                      re.search(r"PINCH DOWN .*hit=node", open(log).read()[mark:]) is not None)
            else:
                print("  [SKIP] gaze+pinch: device has no eye gaze")
        finally:
            op.close()
        check("host survives input", host.poll() is None)
    finally:
        stop(host)


# Display modes. The box is the only scene content, so the frame corner shows
# what is behind it: black (immersive), the simulator room (passthrough) or
# slate (passthrough requested on a system that can't provide it).
MODES = [
    ("immersive", {}, 1, "immersive"),
    ("passthrough", {"VIRO_PASSTHROUGH": "1"}, 3, "passthrough"),
    ("passthrough-fallback", {"VIRO_PASSTHROUGH": "1", "VIRO_XR_DISABLE_EXT": "XR_FB_passthrough"},
     1, "immersive (passthrough fallback)"),
]


def run_modes(profile, workdir):
    for tag, extra, blend, mode in MODES:
        print(f"\n### {profile} / {tag}")
        host, log = launch(profile, "gpu_handle", workdir, tag, extra)
        try:
            if not wait_for(log, r"^session state -> 5"):
                check(f"{tag}: session reaches FOCUSED", False, f"see {log}")
                continue
            time.sleep(2.5)
            text = open(log).read()
            got = re.search(r"^mode: (.*), blend (\d+)$", text, re.M)
            check(f"{tag}: mode and blend", got and got.groups() == (mode, str(blend)),
                  ", ".join(got.groups()) if got else "no mode line")
            if tag == "passthrough-fallback":
                check(f"{tag}: logs the missing capability",
                      "passthrough unavailable: XR_FB_passthrough not enabled" in text)
            op = Operator()
            try:
                img = op.capture("left")
            finally:
                op.close()
            if img is None:
                check(f"{tag}: capture", False)
                continue
            img.save(os.path.join(workdir, f"{profile.replace(' ', '_')}-{tag}-left.png"))
            r, g, b = img.getpixel((20, 20))
            if tag == "immersive":
                check(f"{tag}: background black", r + g + b < 30, f"corner {r},{g},{b}")
            elif tag == "passthrough":
                lit_count, _ = lit(img)
                check(f"{tag}: room visible behind the scene",
                      lit_count > 0.9 * img.size[0] * img.size[1], f"lit {lit_count}")
            else:
                check(f"{tag}: slate background, not black",
                      abs(r - g) < 25 and b > r and r > 60, f"corner {r},{g},{b}")
        finally:
            stop(host)


def build_hello_xr():
    if os.access(HELLO, os.X_OK):
        return True
    cfg = subprocess.run(["cmake", "-S", OPENXR_SRC, "-B", HELLO_BUILD, "-DBUILD_TESTS=ON",
                          "-DBUILD_CONFORMANCE_TESTS=OFF", "-DBUILD_API_LAYERS=OFF",
                          "-DCMAKE_BUILD_TYPE=Release"], capture_output=True, text=True)
    if cfg.returncode == 0:
        cfg = subprocess.run(["cmake", "--build", HELLO_BUILD, "--target", "hello_xr", "-j8"],
                             capture_output=True, text=True)
    if cfg.returncode != 0:
        print(cfg.stdout[-2000:], cfg.stderr[-2000:])
    return os.access(HELLO, os.X_OK)


def run_hello_xr(profile, workdir):
    print(f"\n### {profile} / hello_xr (Metal)")
    if not build_hello_xr():
        check("hello_xr builds", False)
        return
    set_profile(profile)
    log = os.path.join(workdir, f"hello_xr-{profile.replace(' ', '_')}.log")
    env = dict(os.environ,
               XR_RUNTIME_JSON=os.path.join(SIM, "meta_openxr_simulator.json"),
               META_XRSIM_CONFIG_JSON=os.path.join(HERE, "sim-config.json"),
               XR_API_LAYER_PATH=OPERATOR,
               XR_ENABLE_API_LAYERS="XR_APILAYER_METAX_operator")
    # hello_xr quits on the first stdin byte (or EOF), so keep the pipe open.
    app = subprocess.Popen([HELLO, "-g", "Metal", "-v"], cwd=os.path.dirname(HELLO), env=env,
                           stdin=subprocess.PIPE, stdout=open(log, "w"),
                           stderr=subprocess.STDOUT)
    try:
        focused = wait_for(log, r"XR_SESSION_STATE_FOCUSED", timeout=60)
        check("hello_xr: session reaches FOCUSED", focused, "" if focused else f"see {log}")
        if not focused:
            return
        time.sleep(4)
        op = Operator()
        try:
            eyes = [op.capture(e) for e in ("left", "right")]
        finally:
            op.close()
        for e, img in zip(("left", "right"), eyes):
            if img:
                img.save(os.path.join(workdir, f"hello_xr-{profile.replace(' ', '_')}-{e}.png"))
        # hello_xr draws several differently colored cubes; count color buckets.
        buckets = [len({(r // 32, g // 32, b // 32) for r, g, b in img.getdata() if r + g + b > 60})
                   if img else 0 for img in eyes]
        check("hello_xr: both eyes show multi-colored cubes", all(n > 20 for n in buckets),
              f"color buckets L={buckets[0]} R={buckets[1]}")
        check("hello_xr: no XR errors", "XR_ERROR" not in open(log).read())
    finally:
        if app.poll() is None:
            app.stdin.write(b"\n")
            app.stdin.flush()
            try:
                app.wait(timeout=10)
            except subprocess.TimeoutExpired:
                app.kill()
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profiles", default="Meta VR Glasses,Meta Quest 3")
    ap.add_argument("--transports", default="gpu_handle,jpg")
    ap.add_argument("--skip-modes", action="store_true",
                    help="skip the immersive/passthrough/fallback runs")
    ap.add_argument("--skip-hello-xr", action="store_true",
                    help="skip the Khronos hello_xr run")
    args = ap.parse_args()

    if not os.access(HOST, os.X_OK):
        sys.exit(f"build viro_sim_host first: {HOST}")
    if subprocess.run(["pgrep", "-x", "MetaXRSimulator"], capture_output=True).returncode:
        sys.exit("MetaXRSimulator is not running (open -a MetaXRSimulator)")
    op_ok = os.path.isdir(OPERATOR)
    if not op_ok:
        print(f"Meta XR Operator not found at {OPERATOR}; capture and input gates skipped")

    workdir = tempfile.mkdtemp(prefix="xr-sim-gates-")
    backup = PERSIST + ".gates-backup"
    if os.path.exists(PERSIST):
        shutil.copy(PERSIST, backup)
    try:
        for profile in args.profiles.split(","):
            for transport in args.transports.split(","):
                run(profile.strip(), transport.strip(), op_ok, workdir)
            if op_ok and not args.skip_modes:
                run_modes(profile.strip(), workdir)
            if op_ok and not args.skip_hello_xr:
                run_hello_xr(profile.strip(), workdir)
    finally:
        if os.path.exists(backup):
            shutil.move(backup, PERSIST)

    print(f"\nlogs and captures: {workdir}")
    if failures:
        sys.exit(f"{len(failures)} gate(s) failed: " + "; ".join(failures))
    print("all gates passed")


if __name__ == "__main__":
    main()
