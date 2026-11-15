#pragma once
// (0.1.11, VR first person) The physical binocucom's drawing (vr.cpp has its logic): the binocucom itself, a simple
// shaded model in the left hand or on the left thigh, and the round mask of its lens (the picture in picture).
// Built only with -DSLY2_VR=ON; vr.cpp is its only user.

#include <glm/glm.hpp>

namespace vrbinoc
{
// The binocucom into the scene target (depth tested, reversed Z, over the scene as drawn): matClip the eye's clip from
// the binocucom's own space (metres: the left grip's axes, -Z toward its objectives, the left barrel through the
// fist); matTurn its turn in the world (the shading, Z up); uExtend 0..1 the front tubes drawn out (the zoom).
void DrawMesh(const glm::mat4& matClip, const glm::mat3& matTurn, float uExtend);
// The lens's round mask over the scene target's centred square: outside it transparent black (alpha 0), a dark rim,
// inside opaque (alpha 1): the lens's quad blends it as premultiplied.
void DrawLensMask();
// The model's GL objects let go (vr::Stop).
void FreeGL();
// The binocucom's eyepieces' middle in its own space (metres): what is raised to your eyes.
glm::vec3 PosEyepieces();
}  // namespace vrbinoc
