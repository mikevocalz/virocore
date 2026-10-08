#!/usr/bin/env python3
"""Acceptance gates for viro_sim_host against Meta XR Simulator.

Runs the gates from docs/META_XR_SIMULATOR_VULKAN.md on a live simulator,
once per device profile and texture transport, and exits non-zero on any
failure. Needs MetaXRSimulator running, the Meta XR Operator layer installed
(metavr CLI), a built desktop/build/viro_sim_host, and Pillow.

    python3 desktop/tools/xr_sim_gates.py [--profiles "Meta VR Glasses"]
                                          [--transports gpu_handle,raw_rgba,jpg]
                                          [--skip-modes] [--skip-hello-xr]
                                          [--rooms | --skip-rooms]

Per profile it runs: the render and input gates for each transport; the
immersive, passthrough and passthrough-fallback display modes; and Khronos
hello_xr (Metal) on the same runtime, built from the OpenXR source CMake
already fetched into desktop/build/_deps.

The rooms pass (on by default) runs once, on the Meta VR Glasses profile in
passthrough: for each hero room (game room, living room, bedroom) it switches
the synthetic environment with `metavr xrsim env set`, waits for the frontend
to report the new room, launches the host and checks the session, the
passthrough blend, the room behind the box and the textured box. The
environment that was current before the pass is restored at the end.

The input gates drive VROInputControllerOpenXR, the controller that ships on
Quest, PICO and Android XR (the host compiles it from android/sharedCode), so
they test the device input logic, not a desktop stand-in:
  * Glasses, hands only: eye gaze + pinch bind ext/hand_interaction_ext, the
    gaze owns select, a pinch on the box clicks it through the EyeGaze source,
    a pinch while looking away clicks nothing, a held pinch drags the box.
  * Glasses with controllers: select moves to the controller, the trigger
    clicks along the aim ray, and releasing the controllers hands select back
    to the gaze.
  * Quest 3 (no eye tracker): controllers select along the aim ray, and with
    the controllers released a hand pinch selects along the hand ray.
  * Controller models: whatever XR_FB_render_model reports, the log must show
    the runtime model or the documented fallback; runtime models must load,
    and KTX2 (KHR_texture_basisu) textures in them must decode.
  * Per-texture Metal samplers: a magnified checker with a nearest filter
    keeps hard cell edges and with a linear filter blends them.
  * Clean shutdown: the host exits 0 and releases the input controller
    before xrDestroySession.

Two simulator traps this script handles:
  * The config env var is META_XRSIM_CONFIG_JSON. Any other name is ignored
    silently and the simulator runs on its bundled defaults.
  * device_profile in persistent_data.json beats the config file. The script
    writes the profile there for each run and restores the file at the end.
  * Changing the synthetic environment relaunches the simulator's target
    platform and drops every connected OpenXR client, and the choice persists
    across simulator restarts. The rooms pass only switches rooms with no
    host running, and puts the original room back even when a gate fails.
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


# ViroOculus source ids (ViroRenderer/VROInputType.h).
SRC_CONTROLLER = 1
SRC_EYE_GAZE = 12

# Unit quaternions [x, y, z, w] for a yaw (about +Y) of the given degrees.
def yaw(deg):
    import math
    h = math.radians(deg) / 2
    return [0.0, math.sin(h), 0.0, math.cos(h)]


def mark(log):
    return len(open(log).read())


def since(log, at):
    return open(log).read()[at:]


def clicks(text, source):
    return re.findall(rf"^input-xr: BOX CLICK source={source} ", text, re.M)


def bound_profile(op, hand):
    for c in op.call("openxr_get_active_interaction_profile", hand=hand):
        if c.get("type") == "text":
            try:
                return (json.loads(c["text"]).get(hand) or {}).get("profile")
            except ValueError:
                return None
    return None


def box_x(img):
    """Centroid x of the box's orange checker texels. Only the box is orange;
    the controller models (white/black) and the laser (cyan) are excluded, so
    they cannot shift the stereo and head-yaw measurements."""
    w = img.size[0]
    xs = [i % w for i, (r, g, b) in enumerate(img.getdata())
          if r > 150 and 60 < g < 160 and b < 90]
    return sum(xs) / len(xs) if xs else None


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
               VIRO_FRAMES="216000")
    env.update(extra_env or {})
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
        has_gaze = re.search(r"^eye gaze interaction: extension=1 supported=1$", text, re.M) is not None
        suggests = dict(re.findall(r"\[XR-DIAG\] suggest (\S+) -> (\S+)$", text, re.M))
        wanted = ["/interaction_profiles/oculus/touch_controller",
                  "/interaction_profiles/ext/hand_interaction_ext"]
        if has_gaze:
            wanted.append("/interaction_profiles/ext/eye_gaze_interaction")
        check("shared OpenXR controller: bindings accepted",
              all(suggests.get(w) == "XR_SUCCESS" for w in wanted),
              ", ".join(f"{w}={suggests.get(w)}" for w in wanted))
        check("shared OpenXR controller: action set attached",
              "OpenXR action set created and attached" in text and
              "createActionSet failed" not in text)
        check("Metal shaders compile (incl. pointer laser)",
              "Failed to compile dynamic shader library" not in text)
        px = re.search(r"^eye 0 readback .* lit=(\d+)/(\d+)", text, re.M)
        check("Gate A: swapchain readback non-black", px and int(px.group(1)) > 1000,
              f"lit {px.group(1)}/{px.group(2)}" if px else "no readback line")
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
            lx = box_x(eyes["left"]) if eyes["left"] else None
            rx = box_x(eyes["right"]) if eyes["right"] else None
            check("stereo disparity (left centroid right of right's)",
                  lx is not None and rx is not None and lx > rx, f"L={lx} R={rx}")

            op.call("openxr_set_head_pose", position=[0, 1.7, 0],
                    orientation=[0, 0.1736, 0, 0.9848])
            time.sleep(1)
            yawed = op.capture("left")
            op.call("openxr_set_head_pose", position=[0, 1.7, 0], orientation=[0, 0, 0, 1])
            yx = box_x(yawed) if yawed else None
            check("head yaw left moves scene right", yx is not None and lx is not None and yx > lx + 50,
                  f"x {lx} -> {yx}")

            if has_gaze:
                glasses_input(op, log)
            else:
                print("  [SKIP] gaze gates: device has no eye gaze")
                quest_input(op, log)
            controller_models(log)
        finally:
            op.close()
        check("host survives input", host.poll() is None)
    finally:
        stop(host)


def controller_select(op, log, label):
    """A controller aimed at the box from the head selects it on trigger."""
    at = mark(log)
    op.call("openxr_set_controller_pose", hand="right", pose_type="aim",
            position=[0.0, 0.0, 0.0], orientation=[0, 0, 0, 1], base_space="view")
    time.sleep(1.5)
    check(f"{label}: controller profile bound",
          (bound_profile(op, "right") or "").endswith("touch_controller_plus") or
          (bound_profile(op, "right") or "").endswith("touch_controller"),
          str(bound_profile(op, "right")))
    op.call("openxr_set_controller_input", hand="right", component="Trigger", value=1.0)
    time.sleep(0.8)
    op.call("openxr_set_controller_input", hand="right", component="Trigger", value=0.0)
    time.sleep(1)
    text = since(log, at)
    # The simulator starts with its own controllers bound, so the owner may
    # already be the controller: accept the log line or no change since start.
    owner = re.findall(r"Select pointer: (\w+)", open(log).read())
    check(f"{label}: select owner is the controller", owner and owner[-1] == "controller",
          owner[-1] if owner else "no owner line")
    check(f"{label}: trigger clicks the box via the aim ray",
          len(clicks(text, SRC_CONTROLLER)) >= 1, f"{len(clicks(text, SRC_CONTROLLER))} clicks")


def glasses_input(op, log):
    # Hands only: eye gaze + pinch. The simulator binds hand_interaction_ext
    # once gaze/hand automation starts.
    at = mark(log)
    op.call("openxr_set_eye_gaze_pose", orientation=[0, 0, 0, 1], base_space="local")
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1.5)
    profile = bound_profile(op, "right")
    check("glasses hands: active profile hand_interaction_ext",
          profile == "/interaction_profiles/ext/hand_interaction_ext", str(profile))
    text = since(log, at)
    check("glasses hands: app sees hand_interaction_ext bound",
          "active profile /user/hand/right: /interaction_profiles/ext/hand_interaction_ext" in text)
    check("glasses hands: Select pointer: gaze", "Select pointer: gaze" in text)

    at = mark(log)
    op.call("openxr_hand_gesture", hand="right", gesture="pinch")
    time.sleep(0.8)
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1)
    text = since(log, at)
    check("glasses hands: gaze at box + pinch clicks it (EyeGaze source)",
          len(clicks(text, SRC_EYE_GAZE)) == 1,
          f"EyeGaze clicks={len(clicks(text, SRC_EYE_GAZE))} other="
          f"{len(re.findall(r'BOX CLICK', text)) - len(clicks(text, SRC_EYE_GAZE))}")

    at = mark(log)
    op.call("openxr_set_eye_gaze_pose", orientation=yaw(60), base_space="local")
    time.sleep(1)
    op.call("openxr_hand_gesture", hand="right", gesture="pinch")
    time.sleep(0.8)
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1)
    text = since(log, at)
    check("glasses hands: gaze away + pinch clicks nothing",
          not re.search(r"BOX CLICK", text), f"{len(re.findall(r'BOX CLICK', text))} clicks")

    # Drag: pinch on the box, look 10 degrees left while holding, release.
    op.call("openxr_set_eye_gaze_pose", orientation=[0, 0, 0, 1], base_space="local")
    time.sleep(1)
    at = mark(log)
    op.call("openxr_hand_gesture", hand="right", gesture="pinch")
    time.sleep(0.6)
    op.call("openxr_set_eye_gaze_pose", orientation=yaw(10), base_space="local")
    time.sleep(1.2)
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1)
    drags = re.findall(r"^input-xr: box drag source=12 -> \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)",
                       since(log, at), re.M)
    last = tuple(float(v) for v in drags[-1]) if drags else None
    # FixedDistance at 1.5 m: a 10 degree look-left moves the box ~0.26 m to -x.
    check("glasses hands: held pinch drags the box with the gaze",
          last is not None and last[0] < -0.15, f"last drag position {last}")
    # Drag it back so the controller gates aim at a centred box.
    at = mark(log)
    op.call("openxr_hand_gesture", hand="right", gesture="pinch")
    time.sleep(0.6)
    op.call("openxr_set_eye_gaze_pose", orientation=[0, 0, 0, 1], base_space="local")
    time.sleep(1.2)
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1)
    drags = re.findall(r"^input-xr: box drag source=12 -> \((-?[\d.]+), (-?[\d.]+), (-?[\d.]+)\)",
                       since(log, at), re.M)
    last = tuple(float(v) for v in drags[-1]) if drags else None
    check("glasses hands: drag back recentres the box",
          last is not None and abs(last[0]) < 0.05, f"last drag position {last}")

    # Controllers enabled: select moves to the controller.
    controller_select(op, log, "glasses controllers")

    # Releasing the controllers hands select back to the gaze.
    at = mark(log)
    op.call("openxr_release_input_devices")
    time.sleep(1.5)
    op.call("openxr_gaze_and_pinch", gaze_orientation=[0, 0, 0, 1], hand="right",
            hold_duration=0.5, base_space="local")
    time.sleep(2)
    text = since(log, at)
    owners = re.findall(r"Select pointer: (\w+)", text)
    check("glasses controllers released: select back to gaze", "gaze" in owners,
          " -> ".join(owners))
    check("glasses controllers released: gaze+pinch clicks the box",
          len(clicks(text, SRC_EYE_GAZE)) >= 1, f"{len(clicks(text, SRC_EYE_GAZE))} clicks")


def quest_input(op, log):
    controller_select(op, log, "quest controllers")

    # Controllers released: the hand ray selects. Wrist 10 cm right of the
    # head, pointing ahead; the box is 0.5 m wide at 1.5 m.
    at = mark(log)
    op.call("openxr_release_input_devices")
    time.sleep(1.5)
    op.call("openxr_hand_gesture", hand="right", gesture="aim", wrist_position=[0.1, 0.0, -0.3],
            wrist_orientation=[0, 0, 0, 1], base_space="local")
    time.sleep(1)
    op.call("openxr_hand_gesture", hand="right", gesture="pinch")
    time.sleep(0.8)
    op.call("openxr_hand_gesture", hand="right", gesture="open")
    time.sleep(1)
    text = since(log, at)
    owners = re.findall(r"Select pointer: (\w+)", text)
    check("quest hands: Select pointer: hand", "hand" in owners, " -> ".join(owners))
    check("quest hands: pinch clicks the box via the hand ray",
          len(clicks(text, SRC_CONTROLLER)) >= 1, f"{len(clicks(text, SRC_CONTROLLER))} clicks")


def controller_models(log):
    text = open(log).read()
    rm = re.search(r"^\[XR-DIAG\] XR_FB_render_model: extension=(\d) supported=(\d)$", text, re.M)
    if not rm:
        check("controller models: XR_FB_render_model reported", False, "no line")
        return
    supported = rm.group(2) == "1"
    print(f"  [INFO] XR_FB_render_model extension={rm.group(1)} supported={rm.group(2)}")
    if not supported:
        check("controller models: documented fallback used",
              re.search(r"controller model: neutral fallback .*controller_neutral\.glb", text) is not None)
        return
    runtime = set(re.findall(r"controller model: runtime render model /model_fb/controller/(left|right)", text))
    check("controller models: runtime models for both hands", runtime == {"left", "right"},
          ", ".join(sorted(runtime)) or "none")
    check("controller models: glTF loads", "controller mesh load failed" not in text and
          "runtime model failed" not in text)
    if "KHR_texture_basisu" in text:
        decoded = re.findall(r"glTF KHR_texture_basisu: image \d+ '[^']*' decoded: (.*?)\"?$", text, re.M)
        failed = re.findall(r"glTF KHR_texture_basisu: .*(fail|error)", text, re.I)
        check("controller models: KTX2 textures decode", decoded and not failed,
              f"{len(decoded)} decoded, e.g. {decoded[0] if decoded else '-'}")


# Display modes. The box is the only scene content, so the frame corner shows
# what is behind it: black (immersive), the simulator room (passthrough) or
# slate (passthrough requested on a system that can't provide it).
MODES = [
    ("immersive", {}, 1, "immersive"),
    ("passthrough", {"VIRO_PASSTHROUGH": "1"}, 3, "passthrough"),
    ("passthrough-fallback", {"VIRO_PASSTHROUGH": "1", "VIRO_XR_DISABLE_EXT": "XR_FB_passthrough"},
     1, "immersive (passthrough fallback)"),
]


def background(img):
    """The colour most of the four frame corners agree on. The runtime's
    controller models can sit in one corner (the left controller rests top
    left on both profiles), so a single corner is not the background."""
    w, h = img.size
    corners = [img.getpixel(p) for p in ((20, 20), (w - 21, 20), (20, h - 21), (w - 21, h - 21))]
    buckets = {}
    for c in corners:
        buckets.setdefault(tuple(v // 16 for v in c), []).append(c)
    return max(buckets.values(), key=len)[0]


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
            r, g, b = background(img)
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


METAVR = ["npx", "-y", "metavr@latest"]
HERO_ROOMS = ("GameRoom", "LivingRoom", "Bedroom")


def xrsim(*args, timeout=120):
    """Runs `metavr xrsim ... --format json`; returns the parsed JSON or None."""
    r = subprocess.run(METAVR + ["xrsim", *args, "--format", "json"],
                       capture_output=True, text=True, timeout=timeout)
    if r.returncode != 0:
        print(f"  [INFO] metavr xrsim {' '.join(args)} exit {r.returncode}: "
              f"{(r.stderr or r.stdout).strip()[-300:]}")
        return None
    try:
        return json.loads(r.stdout)
    except ValueError:
        return None


def environments():
    return xrsim("env", "list") or []


def current_environment():
    return next((e["id"] for e in environments() if e.get("current")), None)


def ses_processes():
    """(pid, parent pid, room) for each running SyntheticEnvironmentServer.
    The frontend starts it as `SyntheticEnvironmentServer <Room> -batchmode`."""
    out = subprocess.run(["ps", "-axo", "pid=,ppid=,command="],
                         capture_output=True, text=True).stdout
    procs = []
    for line in out.splitlines():
        f = line.split()
        # Match the executable, not any command line that mentions it.
        if len(f) >= 3 and f[2].endswith("/MacOS/SyntheticEnvironmentServer"):
            procs.append((int(f[0]), int(f[1]), f[3] if len(f) > 3 else None))
    return procs


def ses_room():
    """The room of the server a live frontend owns, or None. An orphan
    (parent pid 1) left by an earlier quit does not count."""
    return next((room for _, ppid, room in ses_processes() if ppid != 1), None)


def stop_orphaned_ses():
    """Terminates SyntheticEnvironmentServer processes whose frontend is gone.

    `app quit` can leave the server running, reparented to launchd. It keeps
    the server port, so the next frontend cannot start its own server and the
    old room stays on screen. Only orphans (parent pid 1) are touched, and only
    after the frontend has quit."""
    pids = [pid for pid, ppid, _ in ses_processes() if ppid == 1]
    for pid in pids:
        print(f"  [INFO] stopping orphaned environment server pid {pid}")
        subprocess.run(["kill", str(pid)])
    end = time.time() + 15
    while time.time() < end and any(p in pids for p, _, _ in ses_processes()):
        time.sleep(0.5)


def frontend_ready(timeout=60):
    """Waits until the frontend answers `runtime list` (frontend tier)."""
    end = time.time() + timeout
    while time.time() < end:
        if xrsim("runtime", "list", "--timeout", "2") is not None:
            return True
        time.sleep(2)
    return False


def wait_ses(name, timeout):
    """Waits for a server running `name`. The feature rooms' launch argument
    is not their display name, so for those any running server counts."""
    end = time.time() + timeout
    while time.time() < end:
        room = ses_room()
        if room == name or (room and name not in HERO_ROOMS):
            return True
        time.sleep(1)
    return False


def set_environment(env_id, name):
    """Switches the synthetic environment and waits until the room is served.

    `env set` stops the target platform (the SyntheticEnvironmentServer) to
    relaunch it in the new room. On simulator 207 the frontend stops it but
    does not start it again, and the choice is already persisted, so when no
    server comes back the frontend is restarted (scoped, graceful `app quit`,
    then `app launch`), which starts the server in the persisted room. A
    server orphaned by an earlier quit is stopped first (stop_orphaned_ses). Ready
    means the frontend answers, reports env_id as current, and a server runs
    with this room's name."""
    # Set and restart up to three times: a restart can race the server's
    # startup and come back without one.
    for attempt in range(3):
        if wait_ses(name, 0.1) and current_environment() == env_id:
            break
        if xrsim("env", "set", "--id", env_id) is None:
            return False
        if wait_ses(name, 20):
            break
        print(f"  [INFO] no environment server for {name} after env set "
              f"(attempt {attempt + 1}); restarting the simulator frontend")
        xrsim("app", "quit")
        stop_orphaned_ses()
        if xrsim("app", "launch") is None or not frontend_ready():
            return False
        wait_ses(name, 30)
    ok = frontend_ready() and wait_ses(name, 60) and current_environment() == env_id
    if not ok:
        logs = subprocess.run(METAVR + ["xrsim", "runtime", "logs"],
                              capture_output=True, text=True).stdout
        print(f"  [INFO] environment {env_id} not ready (server room {ses_room()})\n{logs}")
        return False
    time.sleep(3)  # let the server finish loading the room before a client connects
    return True


def room_diff(a, b):
    """Mean absolute per-channel difference of two captures at 64x64."""
    a, b = a.resize((64, 64)), b.resize((64, 64))
    da, db = list(a.getdata()), list(b.getdata())
    return sum(abs(x - y) for p, q in zip(da, db) for x, y in zip(p, q)) / (len(da) * 3)


def run_rooms(workdir, profile="Meta VR Glasses"):
    """Passthrough on each hero room. Leaves the original environment current."""
    envs = environments()
    original = next((e for e in envs if e.get("current")), None)
    rooms = [e for name in HERO_ROOMS for e in envs if e.get("name") == name]
    print(f"\n### rooms ({profile}, passthrough), original environment "
          f"{original['id'] if original else None}")
    check("rooms: simulator lists the three hero rooms", len(rooms) == len(HERO_ROOMS),
          ", ".join(e["id"] for e in rooms))
    if not rooms or original is None:
        check("rooms: current environment readable", original is not None)
        return
    captures = {}
    try:
        for room in rooms:
            name, env_id = room["name"], room["id"]
            print(f"\n### {profile} / passthrough in {name} ({env_id})")
            if not set_environment(env_id, name):
                check(f"room {name}: environment active", False)
                continue
            check(f"room {name}: environment active", True)
            host, log = launch(profile, "gpu_handle", workdir, f"room-{name}",
                               {"VIRO_PASSTHROUGH": "1"})
            try:
                if not wait_for(log, r"^session state -> 5", timeout=40):
                    check(f"room {name}: session reaches FOCUSED", False, f"see {log}")
                    continue
                check(f"room {name}: session reaches FOCUSED", True)
                time.sleep(2.5)
                text = open(log).read()
                got = re.search(r"^mode: (.*), blend (\d+)$", text, re.M)
                check(f"room {name}: mode passthrough, blend 3",
                      got is not None and got.groups() == ("passthrough", "3"),
                      ", ".join(got.groups()) if got else "no mode line")
                floor = re.search(r"^Reference space: (\S+)", text, re.M)
                if floor:
                    check(f"room {name}: floor-level reference space",
                          floor.group(1) == "LOCAL_FLOOR", floor.group(1))
                else:
                    print(f"  [SKIP] room {name}: floor alignment: the desktop host creates a "
                          "LOCAL reference space and logs no 'Reference space:' line")
                op = Operator()
                try:
                    img = op.capture("left")
                finally:
                    op.close()
                if img is None:
                    check(f"room {name}: capture", False)
                    continue
                img.save(os.path.join(workdir, f"room-{name}-left.png"))
                lit_count, _ = lit(img)
                check(f"room {name}: room visible behind the box",
                      lit_count > 0.9 * img.size[0] * img.size[1], f"lit {lit_count}")
                # Count inside the box only: the game room's wood panelling
                # passes the orange test, so a whole-frame count could pass
                # with no box at all. The box spans roughly 39-65% of the
                # width and 35-65% of the height of the left eye; this crop
                # sits inside it and must be about half orange, half white.
                w, h = img.size
                px = list(img.crop((int(w * .42), int(h * .38),
                                    int(w * .62), int(h * .62))).getdata())
                orange = sum(1 for r, g, b in px if r > 150 and 60 < g < 160 and b < 90)
                white = sum(1 for r, g, b in px if r > 170 and g > 170 and b > 170)
                check(f"room {name}: box rendered with checker texture",
                      orange > 0.25 * len(px) and white > 0.25 * len(px),
                      f"orange={orange} white={white} of {len(px)} box pixels")
                captures[name] = img
                check(f"room {name}: host survives", host.poll() is None)
            finally:
                stop(host)
        # Each room must actually change what passthrough shows; a stale
        # environment would hand back the same frame for every room.
        names = list(captures)
        for i, a in enumerate(names):
            for b in names[i + 1:]:
                d = room_diff(captures[a], captures[b])
                check(f"rooms: {a} and {b} passthrough differ", d > 8, f"mean diff {d:.1f}")
    finally:
        restored = set_environment(original["id"], original["name"])
        check("rooms: original environment restored",
              restored and current_environment() == original["id"], str(current_environment()))


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


def run_sampler(profile, workdir):
    """Per-texture Metal samplers. The host swaps in an 8x8 checker magnified
    ~30x and sets its filter mode on the VROTexture; nearest must keep the
    cell edges hard, linear must blend them. Both run through the texture's
    own MTLSamplerState, so a shared shader-constant sampler fails one side."""
    print(f"\n### {profile} / per-texture sampler")
    blend = {}
    for mode in ("nearest", "linear"):
        host, log = launch(profile, "gpu_handle", workdir, f"sampler-{mode}",
                           {"VIRO_TEXTURE_FILTER": mode})
        try:
            if not wait_for(log, r"^session state -> 5"):
                check(f"sampler {mode}: session reaches FOCUSED", False, f"see {log}")
                return
            time.sleep(2.5)
            check(f"sampler {mode}: host applied the filter",
                  f"texture filter: {mode}" in open(log).read())
            op = Operator()
            try:
                img = op.capture("left")
            finally:
                op.close()
        finally:
            stop(host)
        if img is None:
            check(f"sampler {mode}: capture", False)
            return
        img.save(os.path.join(workdir, f"{profile.replace(' ', '_')}-sampler-{mode}-left.png"))
        # Warm but between the orange cell (g~120) and the white cell: only a
        # filtered cell edge produces these. r - b keeps a shaded white face
        # (grey, r ~ b) from counting as a blend.
        blend[mode] = sum(1 for r, g, b in img.getdata()
                          if r > 150 and 160 <= g <= 200 and 60 <= b <= 180
                          and r - b >= 60)
    check("sampler: nearest keeps cell edges hard, linear blends them",
          blend["linear"] > 2000 and blend["linear"] > 10 * max(blend["nearest"], 1),
          f"blend pixels nearest={blend['nearest']} linear={blend['linear']}")


def run_shutdown(profile, workdir):
    """A short frame budget ends the session the normal way: the host must tear
    the input controller down before xrDestroySession and exit 0."""
    print(f"\n### {profile} / clean shutdown")
    host, log = launch(profile, "gpu_handle", workdir, "shutdown", {"VIRO_FRAMES": "400"})
    try:
        rc = host.wait(timeout=120)
    except subprocess.TimeoutExpired:
        host.kill()
        rc = None
    text = open(log).read()
    check("shutdown: host exits 0 after its frame budget", rc == 0 and "viro_sim_host ok" in text,
          f"exit {rc}")
    check("shutdown: input controller released before the session",
          "controller still owned at teardown" not in text)
    time.sleep(2)


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
    ap.add_argument("--profiles", default="Meta VR Glasses",
                    help="comma-separated simulator device profiles, e.g. "
                         "\"Meta VR Glasses,Meta Quest 3\"")
    ap.add_argument("--transports", default="gpu_handle,raw_rgba,jpg")
    ap.add_argument("--skip-modes", action="store_true",
                    help="skip the immersive/passthrough/fallback runs")
    ap.add_argument("--skip-hello-xr", action="store_true",
                    help="skip the Khronos hello_xr run")
    rooms = ap.add_mutually_exclusive_group()
    rooms.add_argument("--rooms", dest="rooms", action="store_true", default=True,
                       help="run the hero-room passthrough pass (default)")
    rooms.add_argument("--skip-rooms", dest="rooms", action="store_false",
                       help="skip the hero-room passthrough pass")
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
            run_shutdown(profile.strip(), workdir)
            if op_ok:
                run_sampler(profile.strip(), workdir)
            if op_ok and not args.skip_modes:
                run_modes(profile.strip(), workdir)
            if op_ok and not args.skip_hello_xr:
                run_hello_xr(profile.strip(), workdir)
        if op_ok and args.rooms:
            run_rooms(workdir)
        elif args.rooms:
            print("  [SKIP] rooms: needs the Meta XR Operator for captures")
    finally:
        if os.path.exists(backup):
            shutil.move(backup, PERSIST)

    print(f"\nlogs and captures: {workdir}")
    if failures:
        sys.exit(f"{len(failures)} gate(s) failed: " + "; ".join(failures))
    print("all gates passed")


if __name__ == "__main__":
    main()
