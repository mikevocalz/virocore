# Mobbin references — PICO 2D surfaces

Pulled 2026-09-11 for the two 2D surfaces this work introduces (see
`handoff.md`). Each entry records the canonical `mobbin_url` and what the
reference contributes. These are phone screens; the XR surfaces borrow the
information structure, not the chrome.

## Surface 1 — tracking-origin setting (`trackingOrigin: "eye" | "floor"`)

- Halide Mark III, Capture Settings — https://mobbin.com/screens/2c907a1e-953d-40e2-b0a0-b40b6e045e12
  Two mutually-exclusive processing modes as radio cards, each with a one-line
  consequence under the label. Matches our two-mode choice: the option name
  alone ("Eye" / "Floor") doesn't tell a user what changes, so each mode
  carries its consequence line ("content floats at head height" / "y=0 sits on
  your floor").
- Flighty, Units — https://mobbin.com/screens/faccedb1-04b5-46ea-b60a-ac6c15ebc842
  Segmented control per setting with an explanation *above* the control, not
  behind an info icon. Altitude's caption ("Zero on the ground…") is the same
  shape as explaining where y=0 lives — copy this pattern for a settings row
  in a demo app.

## Surface 2 — input-status toast ("Controllers not detected — using hands")

- PayPal, rewards toggle confirmation — https://mobbin.com/screens/14088eee-0c1c-42bc-a467-352076ad1c67
  Top-anchored toast stating the new state in past tense, auto-dismissing,
  no action button. Matches "Floor level set": state the completed change,
  don't ask anything.
- Brick, schedule banner — https://mobbin.com/screens/93b187be-a6f6-4390-8ec3-51b1d68b1b69
  Title + one-line detail + explicit close affordance. Matches the
  hand-tracking hand-off toast, which needs the detail line ("using hands")
  and a dismissal the user controls, since it can arrive mid-task.

Not adopted: modal dialogs or blocking sheets for either surface — both
events are recoverable state changes, and the comfort skill's §4 pattern
(offer, never interrupt) rules out stealing focus in-headset.
