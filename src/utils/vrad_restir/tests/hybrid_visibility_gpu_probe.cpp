// Production Vulkan local-visibility service, with analytic immutable/alpha fixtures.
#include "restir_vulkan.h"
#include "restir_types.h"
#include <stdio.h>
#include <string.h>
#include <math.h>

static void Quad(ReSTIRScene &scene, float x0, float x1, float z, uint32 material, uint32 flags, uint32 hitId)
{
    const float p[4][3] = {{x0,-16,z},{x1,-16,z},{x1,16,z},{x0,16,z}};
    const int indices[2][3] = {{0,1,2},{0,2,3}};
    for (int i = 0; i < 2; ++i)
    {
        ReSTIRGpuTriangle t = {};
        memcpy(t.v0,p[indices[i][0]],12); memcpy(t.v1,p[indices[i][1]],12); memcpy(t.v2,p[indices[i][2]],12);
        t.material = material; t.flags = flags; t.hitId = hitId; t.face = -1;
        scene.triangles.AddToTail(t);
    }
}
int main(int argc, char **argv)
{
    ReSTIROptions options;
    options.shadowMaps = true;
    options.mapPath = "hybrid_visibility_gpu_fixture";
    options.forceComputeBvh = argc > 1 && !strcmp(argv[1],"--software");
    ReSTIRScene scene;
    scene.worldMins.Init(-32,-32,-32); scene.worldMaxs.Init(32,32,32);
    scene.sceneStyles.AddToTail(0);
    // 128 is covered, 127 is transparent under the shared binary alpha contract.
    for (int i = 0; i < 2; ++i)
    {
        ReSTIRSceneTexture &texture = scene.textures[scene.textures.AddToTail()];
        texture.width = texture.height = texture.channels = 1;
        texture.texels.AddToTail(i ? 127 : 128);
        ReSTIRGpuMaterial material = {};
        material.coverageTexture = i; material.albedoTexture = -1;
        material.textureWidth = material.textureHeight = 1;
        scene.materials.AddToTail(material);
    }
    const uint32 immutable = RESTIR_TRI_SHADOW | RESTIR_TRI_STATIC_SUN;
    const uint32 propHit = RESTIR_TRACE_ID_STATICPROP | 7u;
    Quad(scene,0,16,5,0,immutable | RESTIR_TRI_NONOPAQUE,propHit);
    Quad(scene,-16,16,7,1,immutable | RESTIR_TRI_NONOPAQUE,RESTIR_TRACE_ID_OPAQUE);
    // Mutable geometry is transport-shadowing but never an immutable caster.
    Quad(scene,-16,16,2,0,RESTIR_TRI_SHADOW,RESTIR_TRACE_ID_OPAQUE);
    // An opaque immutable plane beyond the emitter cannot occlude a finite segment.
    Quad(scene,-16,16,20,0,immutable,RESTIR_TRACE_ID_OPAQUE);
    for (int i = 0; i < 2; ++i)
    {
        ShadowMapLightDisk selected = {};
        selected.light.type = emit_point; selected.light.origin.Init(0,0,10);
        selected.shadowSourceRadius = i ? 4.0f : 0.0f;
        scene.shadowLights.AddToTail(selected);
    }
    CUtlVector<ReSTIRGpuVisibilityQuery> queries;
    const float x[5] = {-4,4,0,0,0};
    for (int i = 0; i < 5; ++i)
    {
        ReSTIRGpuVisibilityQuery q = {};
        q.position[0] = x[i]; q.position[3] = i == 4 ? 0.0f : 1.0f;
        q.selectedLightIndex = i >= 2 ? 1 : 0;
        if (i == 3) { q.flags = RESTIR_VISIBILITY_NO_SELF_SHADOW; q.skipHitId = propHit; }
        queries.AddToTail(q);
    }
    CReSTIRVulkanDevice device;
    CUtlVector<float> result;
    if (!device.Init(options) || !device.UploadScene(scene) || !device.ComputeLocalVisibility(queries,result)) return 2;
    const float expected[5] = {1,0,0.5f,1,0};
    if (result.Count() != 5) return 3;
    for (int i = 0; i < 5; ++i)
    {
        printf("local_visibility case=%d value=%.9g expected=%.9g\n",i,result[i],expected[i]);
        if (!isfinite(result[i]) || fabsf(result[i]-expected[i]) > 0.000001f) return 4;
    }
    printf("PASS production local visibility backend=%s cases=5 alpha127/128 immutable finite no-self invalid-receiver\n",
        device.GetDeviceInfo().backend == RESTIR_BACKEND_HARDWARE_RT ? "hardware-rt" : "compute-bvh");
    return 0;
}
