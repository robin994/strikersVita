#if defined(STRIKERS_VITA_NATIVE_RUNTIME)
#include "native_match_internal.hpp"

namespace port::directmatch::detail
{
void MatchRuntime::on_packet(eGLView view, unsigned long flags, const glModelPacket* ptr) noexcept
{
    if (!active || !ptr)
        return;
    try
    {
        const auto& p = *ptr;
        using Profile = render::PacketProfile;
        Profile::Scope profile{packetProfile, unsigned(view), p.numVertices,
                               [] { return gpu.pending_frontend_wait_us(); }};
        consume_writes();
        PacketTrace trace{ view, p, invalid };
        // GLX updates material state before sending geometry, so even a packet
        // rejected below must retain its partial TEV updates for later packets.
        observe_material_state(flags, p);
        profile.next(Profile::Arrays);
        PacketGeometry data;
        if (!prepare_packet_geometry(p, data))
            return;

        PreparedMaterial prepared;
        std::span<const GLSkinUserData> skin;
        profile.next(Profile::Skin);
        if (!prepare_skin(p, data.hasPn, prepared.scene, skin))
            return;
        profile.next(Profile::Material);
        if (!prepare_material(view, p, prepared))
            return;
        prepared.raster.primitive = p.primType >= 4 ? 1 : 0;

        uint32_t modelId = 0;
        auto& model = modelScratch;
        model.vertices.clear();
        model.indices.clear();
        struct RecycleGeometry
        {
            ModelVertices& model;
            ModelVertexScratch& scratch;
            ~RecycleGeometry() { recycle_model_vertex_scratch(model, scratch); }
        } recycle{ model, vertexScratch };
        profile.next(Profile::Geometry);
        if (!resolve_model(p, data, skin, model, modelId))
            return;
        trace_ball_packet(view, p, ptr, modelId, data, prepared);
        const auto& matrix = prepared.matrix;
        const auto& raster = prepared.raster;
        const auto& m = prepared.scene;
        profile.next(Profile::Submit);
        if (!gpu.draw_scene(modelId, model.vertices, model.indices, matrix, raster, m))
        {
            failure = "scene_draw";
            ++invalid;
            frameDiscarded = true;
            return;
        }
        profile.submitted(modelId, model.vertices.size(), model.indices.size());
        profile.next(Profile::Cleanup);
        ++draws;
        if (p.state.texconfig)
            ++textured;
        if (view == GLV_Anark)
            ++hud;
        if (view == GLV_Characters)
            ++characters;
        if (!modelId)
            ++dynamic;
    }
    catch (...)
    {
        failure = "draw_allocation";
        ++invalid;
        frameDiscarded = true;
    }
}
}   // namespace port::directmatch::detail
#endif
