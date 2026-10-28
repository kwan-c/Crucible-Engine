/*************************************************************************
  \file       WorldTextPlane.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    Where a WorldText's box is in the world for one camera - the same sums
    WorldUI.slang's TextAxes does on the GPU - for the editor: the
    selected text's outline, clicking text in the Scene view, framing it.
  \details
    Header-only, no ECS or GPU. The box is WorldText::size placed by its
    pivot in the text's plane; with Overflow, text that runs past the box
    is drawn but is not part of it (as Unity's RectTransform).
*************************************************************************/
#pragma once

#include <array>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include "Text/WorldText.h"

namespace Crucible {

	/// The text's plane: its origin (the entity's position) and its +x / +y axes in the world, scale included.
	struct WorldTextPlane {
		glm::vec3 origin{ 0.0f };
		glm::vec3 right{ 1.0f, 0.0f, 0.0f };
		glm::vec3 up{ 0.0f, 1.0f, 0.0f };
	};

	/*************************************************************************
	 \brief  The plane a camera at `cameraPosition` with view matrix `view`
	         sees `text` (on an entity with world matrix `world`) drawn in.
	 *************************************************************************/
	inline WorldTextPlane WorldTextPlaneFor(const WorldText& text, const glm::mat4& world, const glm::vec3& cameraPosition, const glm::mat4& view) {
		WorldTextPlane plane;
		plane.origin = glm::vec3(world[3]);
		plane.right  = glm::vec3(world[0]);
		plane.up     = glm::vec3(world[1]);
		if (text.billboard == WorldText::Billboard::Off) { return plane; }

		const float sx = glm::length(plane.right), sy = glm::length(plane.up);   // the Transform's scale still applies
		if (text.billboard == WorldText::Billboard::FaceCamera) {
			plane.right = glm::vec3(glm::row(view, 0)) * sx;   // the camera's right and up axes
			plane.up    = glm::vec3(glm::row(view, 1)) * sy;
			return plane;
		}

		// Upright: straight up, turned around Y toward the camera.
		glm::vec3 toCamera = cameraPosition - plane.origin;
		toCamera.y = 0.0f;
		if (glm::dot(toCamera, toCamera) < 1.0e-8f) {
			const glm::vec4 back = glm::row(view, 2);
			toCamera = glm::vec3(back.x, 0.0f, back.z);
			if (glm::dot(toCamera, toCamera) < 1.0e-8f) { toCamera = glm::vec3(0.0f, 0.0f, 1.0f); }
		}
		const glm::vec3 facing = glm::normalize(toCamera);
		plane.up    = glm::vec3(0.0f, 1.0f, 0.0f) * sy;
		plane.right = glm::cross(glm::vec3(0.0f, 1.0f, 0.0f), facing) * sx;
		return plane;
	}

	/// The box's extent in the plane: min x, min y, max x, max y (+y up, the entity at 0, 0).
	inline glm::vec4 WorldTextBoxInPlane(const WorldText& text) {
		const glm::vec2 size = glm::max(text.size, glm::vec2(0.0f));
		return glm::vec4(-text.pivot.x * size.x, (text.pivot.y - 1.0f) * size.y,
		                 (1.0f - text.pivot.x) * size.x, text.pivot.y * size.y);
	}

	/// The box's corners in the world: top-left, top-right, bottom-right, bottom-left.
	inline std::array<glm::vec3, 4> WorldTextBoxCorners(const WorldText& text, const WorldTextPlane& plane) {
		const glm::vec4 b = WorldTextBoxInPlane(text);
		auto at = [&](float x, float y) { return plane.origin + plane.right * x + plane.up * y; };
		return { at(b.x, b.w), at(b.z, b.w), at(b.z, b.y), at(b.x, b.y) };
	}

	/*************************************************************************
	 \brief  Does the ray (origin + direction * t, t >= 0) hit the text's box?
	         Either side counts (a single-sided text can still be clicked).
	 \param[out] t  Where, along `direction` (in its units).
	 *************************************************************************/
	inline bool RayHitsWorldTextBox(const WorldText& text, const WorldTextPlane& plane, const glm::vec3& origin, const glm::vec3& direction, float& t) {
		const glm::vec3 normal = glm::cross(plane.right, plane.up);
		const float facing = glm::dot(direction, normal);
		if (std::fabs(facing) < 1.0e-12f) { return false; }   // edge on, or scaled flat
		t = glm::dot(plane.origin - origin, normal) / facing;
		if (t < 0.0f) { return false; }

		// The hit point in the plane's own (possibly skewed) axes.
		const glm::vec3 d = origin + direction * t - plane.origin;
		const float rr = glm::dot(plane.right, plane.right), ru = glm::dot(plane.right, plane.up), uu = glm::dot(plane.up, plane.up);
		const float det = rr * uu - ru * ru;
		if (std::fabs(det) < 1.0e-12f) { return false; }
		const float dr = glm::dot(d, plane.right), du = glm::dot(d, plane.up);
		const float x = (dr * uu - du * ru) / det;
		const float y = (du * rr - dr * ru) / det;
		const glm::vec4 b = WorldTextBoxInPlane(text);
		return x >= b.x && x <= b.z && y >= b.y && y <= b.w;
	}

}   // NAMESPACE CRUCIBLE
