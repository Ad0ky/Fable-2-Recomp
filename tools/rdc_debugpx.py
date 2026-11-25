# fable2 (2026-11-25): steps RenderDoc's shader debugger through the pixel shader of one draw at one pixel (run inside
# qrenderdoc, like rdc_passes.py) and writes every variable change, to see where a wrong output comes from.
#   RDC_IN = capture, RDC_OUT = folder for debugpx.txt, RDC_EVENT = draw event, RDC_PIXEL = "x,y",
#   RDC_PRIM = primitive id (optional, from rdc_history.py / rdc_event.py)
import os
import traceback

import renderdoc as rd

IN = os.environ.get("RDC_IN")
OUT = os.environ.get("RDC_OUT")
EVENT = int(os.environ.get("RDC_EVENT", "0"))
PX, PY = [int(v) for v in os.environ.get("RDC_PIXEL", "0,0").split(",")]
PRIM = os.environ.get("RDC_PRIM", "")
log = open(os.path.join(OUT, "debugpx.txt"), "w")


def say(*parts):
    log.write(" ".join(str(p) for p in parts) + "\n")
    log.flush()


def fmt(var):
    n = max(1, var.columns) * max(1, var.rows)
    n = min(n, 16)
    if var.type in (rd.VarType.SInt, rd.VarType.SShort, rd.VarType.SByte, rd.VarType.SLong):
        vals = [str(var.value.s32v[i]) for i in range(n)]
    elif var.type in (rd.VarType.UInt, rd.VarType.UShort, rd.VarType.UByte, rd.VarType.Bool, rd.VarType.ULong):
        vals = [str(var.value.u32v[i]) for i in range(n)]
    elif var.type == rd.VarType.Double:
        vals = ["%.6g" % var.value.f64v[i] for i in range(n)]
    else:
        vals = ["%.6g" % var.value.f32v[i] for i in range(n)]
    out = var.name + " = " + " ".join(vals)
    for m in var.members:
        out += " {" + fmt(m) + "}"
    return out


def run(controller):
    controller.SetFrameEvent(EVENT, True)
    inputs = rd.DebugPixelInputs()
    if PRIM:
        inputs.primitive = int(PRIM)
    trace = controller.DebugPixel(PX, PY, inputs)
    if trace is None or trace.debugger is None:
        say("no debug trace for pixel", PX, PY, "at event", EVENT)
        return
    say("event", EVENT, "pixel", PX, PY)
    for v in trace.inputs:
        say("  input", fmt(v))
    steps = 0
    while True:
        states = controller.ContinueDebug(trace.debugger)
        if len(states) == 0:
            break
        for s in states:
            steps += 1
            for c in s.changes:
                say("  step", s.stepIndex, "inst", s.nextInstruction, fmt(c.after))
    say("steps", steps)
    controller.FreeTrace(trace)


def main():
    cap = rd.OpenCaptureFile()
    status = cap.OpenFile(IN, "", None)
    if status != rd.ResultCode.Succeeded:
        say("open failed", status)
        return
    result = cap.OpenCapture(rd.ReplayOptions(), None)
    status, controller = result if isinstance(result, tuple) else (rd.ResultCode.Succeeded, result)
    if status != rd.ResultCode.Succeeded:
        say("replay failed", status)
        return
    try:
        run(controller)
    finally:
        controller.Shutdown()
        cap.Shutdown()


try:
    main()
except Exception:
    say(traceback.format_exc())
say("done")
log.close()
os._exit(0)
