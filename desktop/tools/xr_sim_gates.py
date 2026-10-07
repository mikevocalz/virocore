#!/usr/bin/env python3
"""Acceptance gates for viro_sim_host against Meta XR Simulator.

Runs the gates from docs/META_XR_SIMULATOR_VULKAN.md on a live simulator,
once per device profile and texture transport, and exits non-zero on any
failure. Needs MetaXRSimulator running, the Meta XR Operator layer installed
(metavr CLI), a built desktop/build/viro_sim_host, and Pillow.

    python3 desktop/tools/xr_sim_gates.py [--profiles "Meta VR Glasses,Meta Quest 3"]
                                          [--transports gpu_handle,jpg]

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


def run(profile, transport, op_ok, workdir):
    print(f"\n### {profile} / ses_texture_format={transport}")
    cfg = json.load(open(os.path.join(HERE, "sim-config.json")))
    cfg["device_profile"], cfg["ses_texture_format"] = profile, transport
    cfg_path = os.path.join(workdir, "sim-config.json")
    json.dump(cfg, open(cfg_path, "w"))
    set_profile(profile)

    log = os.path.join(workdir, f"host-{profile.replace(' ', '_')}-{transport}.log")
    env = dict(os.environ,
               XR_RUNTIME_JSON=os.path.join(SIM, "meta_openxr_simulator.json"),
               META_XRSIM_CONFIG_JSON=cfg_path,
               XR_API_LAYER_PATH=OPERATOR,
               XR_ENABLE_API_LAYERS="XR_APILAYER_METAX_operator",
               VIRO_FRAMES="216000")
    host = subprocess.Popen([HOST], cwd=os.path.dirname(HOST), env=env,
                            stdout=open(log, "w"), stderr=subprocess.STDOUT)
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
        host.terminate()
        host.wait(timeout=10)
        time.sleep(2)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--profiles", default="Meta VR Glasses,Meta Quest 3")
    ap.add_argument("--transports", default="gpu_handle,jpg")
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
    finally:
        if os.path.exists(backup):
            shutil.move(backup, PERSIST)

    print(f"\nlogs and captures: {workdir}")
    if failures:
        sys.exit(f"{len(failures)} gate(s) failed: " + "; ".join(failures))
    print("all gates passed")


if __name__ == "__main__":
    main()
