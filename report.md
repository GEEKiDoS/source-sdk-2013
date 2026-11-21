# PostFeatures report

Branch `perf/PostFeatures` on `perf/integ2`. `42a318ae49`: bloom quality 1/2. `e265a33b0d`: real depth and network transmission for DOF. `4e5e556df1`: compact blurred vignette and particle-driven vomit.

On a fixed street view at 1920×1080 output (FSR at 0.667), the fallback reference showed Normal at 2.203 ms steady, 3.116 ms with blurred vignette and 4.155 ms with actual vomit (`cmd it`). Both exceeded the 0.5-ms event threshold. The native implementation keeps the packed luminance field and composes the radial blur or particle refraction and tint before the LUT. All three native bloom qualities retain the rebased storage layout.

The client and shader pass the controller lanes as one frame snapshot. View fades and controller fade-to-black remain with the legacy grade; UI/stats transition blur still runs in its original client stage. In the captured entry/exit sequences these matched Extra without an additional flash. DOF had been disabled in both modes because DX12 reports integer HDR. It now reads the actual scene depth, not opaque-material alpha; the controller is transmitted even outside the PVS.

Evidence: `C:/l4d2_work/perf1050/PostFeatures/report.md`, `shots/` and the raw `samples.json` in each capture directory. The full shader-pack, shader DLLs, client and server built under the machine lock. The DX12 debug run with blur, vomit and DOF changed level and exited normally without reported debug-layer errors. The optional DX9 capture did not acquire a local camera, so DX9 image equivalence has not been measured.