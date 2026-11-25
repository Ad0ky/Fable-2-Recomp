# fable2 (2026-11-25): the samplers chosen draws of a RenderDoc capture use, per stage (run inside qrenderdoc, like
# rdc_passes.py): filter, address modes and LOD range of each sampler the vertex and pixel shaders read, next to the
# textures they read, to compare two draws that should sample the same texture the same way.
#   RDC_IN = capture, RDC_OUT = folder for samplers.txt, RDC_EVENTS = "e1,e2,..."
import os
import traceback

import renderdoc as rd

IN = os.environ.get("RDC_IN")
OUT = os.environ.get("RDC_OUT")
EVENTS = [int(e) for e in os.environ.get("RDC_EVENTS", "").split(",") if e.strip()]
log = open(os.path.join(OUT, "samplers.txt"), "w")


def say(*parts):
    log.write(" ".join(str(p) for p in parts) + "\n")
    log.flush()


try:
    cap = rd.OpenCaptureFile()
    if cap.OpenFile(IN, "", None) != rd.ResultCode.Succeeded:
        raise RuntimeError("open file")
    result = cap.OpenCapture(rd.ReplayOptions(), None)
    status, controller = result if isinstance(result, tuple) else (rd.ResultCode.Succeeded, result)
    for ev in EVENTS:
        controller.SetFrameEvent(ev, True)
        pipe = controller.GetPipelineState()
        say("event", ev)
        for stage, name in ((rd.ShaderStage.Vertex, "vs"), (rd.ShaderStage.Pixel, "ps")):
            for used in pipe.GetReadOnlyResources(stage, True):
                say("  %s tex %d" % (name, int(used.descriptor.resource)))
            for used in pipe.GetSamplers(stage, True):
                s = used.sampler
                say("  %s sampler filter %s/%s/%s %s addr %s %s %s lod %.3g..%.3g bias %.3g" % (
                    name, s.filter.minify, s.filter.magnify, s.filter.mip, s.filter.filter,
                    s.addressU, s.addressV, s.addressW, s.minLOD, s.maxLOD, s.mipBias))
    controller.Shutdown()
    cap.Shutdown()
    say("done")
except Exception:
    say("ERROR", traceback.format_exc())
log.close()
os._exit(0)
