# PostFeatures report

Branch `perf/PostFeatures`; commit `42a318ae49`.

Completed: retail/native post audit in `audit.md`; removed the compact fuse restriction requiring `mat_postfx_bloom_quality == 0`, so compact composition remains eligible at bloom quality 1 and 2 using the existing populated/rebased mip selection.

Audit conclusion: LUT, grain, ordinary vignette, local contrast, native AgX/bloom, motion blur and glow are represented. Blurred vignette/distant blur, top vignette, fade-to-black, explicit view fades, vomit refraction, transition blur and Terror DOF are not all represented by the compact shader. Existing code falls back to legacy TerrorEnginePost/client paths for several. This report does not claim native implementation for those features.

Verification: this partial pass was not rebuilt or feature-isolated in-game. No Normal-vs-Extra feature screenshots are claimed. Parent evidence is in `C:/l4d2_work/perf1050/PostLean/`.