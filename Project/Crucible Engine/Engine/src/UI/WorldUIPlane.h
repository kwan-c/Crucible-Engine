/*************************************************************************
  \file       WorldUIPlane.h
  \author     Tan Chun Kwang Xavier
  \par        chunkwangxavier.tan@digipen.edu
  \date       28 October 2026
  \brief
    Where a world-space UI panel - a WorldText's box, a world canvas's page
    - is in the world for one camera: the same sums WorldUI.slang's
    TextAxes does on the GPU. For the editor and debug lines: outlines,
    clicking in the Scene view, framing.
  \details
    Header-only, no ECS or GPU. A panel lies in a plane through its entity:
    +x right, +y up, read from the +z side, its axes the Transform's (scale
    included) or turned to the camera (WorldBillboard). A box in that plane
    is min x, min y, max x, max y in its own units (metres before scale).
      - WorldText: its size placed by its pivot. With Overflow, text that
        runs past the box is drawn but is not part of it (as Unity).
      - A world canvas: its page, referenceResolution / pixelsPerUnit,
        placed by its pivot; page pixels map in with WorldCanvasToPlane.
*************************************************************************/
#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_access.hpp>

#include "Text/WorldText.h"
#include "UI/UIComponents.h"

namespace Crucible {

	/// A panel's plane: its origin (the entity's position) and its +x / +y axes in the world, scale included.
	struct WorldUIPlane {
		glm::vec3 origin{ 0.0f };
		glm::vec3 right{ 1.0f, 0.0f, 0.0f };
		glm::vec3 up{ 0.0f, 1.0f, 0.0f };
	};

	/*************************************************************************
	 \brief  The plane a camera at `cameraPosition` with view matrix `view`
	         sees a panel drawn in, for an entity with world matrix `world`.
	 *************************************************************************/
	inline WorldUIPlane WorldUIPlaneFor(WorldBillboard billboard, const glm::mat4& world, const glm::vec3& cameraPosition, const glm::mat4& view) {
		WorldUIPlane plane;
		plane.origin = glm::vec3(world[3]);
		plane.right  = glm::vec3(world[0]);
		plane.up     = glm::vec3(world[1]);
		if (billboard == WorldBillboard::Off) { return plane; }

		const float sx = glm::length(plane.right), sy = glm::length(plane.up);   // the Transform's scale still applies
		if (billboard == WorldBillboard::FaceCamera) {
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

	/// A point of the plane (+y up) in the world.
	inline glm::vec3 WorldUIPlanePoint(const WorldUIPlane& plane, glm::vec2 p) {
		return plane.origin + plane.right * p.x + plane.up * p.y;
	}

	/// A box's corners in the world: top-left, top-right, bottom-right, bottom-left.
	inline std::array<glm::vec3, 4> WorldUIBoxCorners(const WorldUIPlane& plane, const glm::vec4& box) {
		return { WorldUIPlanePoint(plane, { box.x, box.w }), WorldUIPlanePoint(plane, { box.z, box.w }),
		         WorldUIPlanePoint(plane, { box.z, box.y }), WorldUIPlanePoint(plane, { box.x, box.y }) };
	}

	/// Where a world point (assumed in the plane) is in the plane's own (possibly skewed) axes, +y up.
	inline glm::vec2 WorldUIPlaneLocal(const WorldUIPlane& plane, const glm::vec3& point) {
		const glm::vec3 d = point - plane.origin;
		const float rr = glm::dot(plane.right, plane.right), ru = glm::dot(plane.right, plane.up), uu = glm::dot(plane.up, plane.up);
		const float det = rr * uu - ru * ru;
		if (std::fabs(det) < 1.0e-12f) { return glm::vec2(0.0f); }
		const float dr = glm::dot(d, plane.right), du = glm::dot(d, plane.up);
		return glm::vec2((dr * uu - du * ru) / det, (du * rr - dr * ru) / det);
	}

	/*************************************************************************
	 \brief  Does the ray (origin + direction * t, t >= 0) hit the box in the
	         plane? Either side counts (a single-sided panel can still be
	         clicked).
	 \param[out] t      Where, along `direction` (in its units).
	 \param[out] local  Where in the plane (+y up), when it hits. May be null.
	 *************************************************************************/
	inline bool RayHitsWorldUIBox(const WorldUIPlane& plane, const glm::vec4& box, const glm::vec3& origin, const glm::vec3& direction,
	                              float& t, glm::vec2* local = nullptr) {
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
		if (local) { *local = glm::vec2(x, y); }
		return x >= box.x && x <= box.z && y >= box.y && y <= box.w;
	}

	// ---- WorldText ------------------------------------------------------------------------------

	inline WorldUIPlane WorldTextPlaneFor(const WorldText& text, const glm::mat4& world, const glm::vec3& cameraPosition, const glm::mat4& view) {
		return WorldUIPlaneFor(text.billboard, world, cameraPosition, view);
	}

	/// The text's box in its plane: min x, min y, max x, max y (+y up, the entity at 0, 0).
	inline glm::vec4 WorldTextBoxInPlane(const WorldText& text) {
		const glm::vec2 size = glm::max(text.size, glm::vec2(0.0f));
		return glm::vec4(-text.pivot.x * size.x, (text.pivot.y - 1.0f) * size.y,
		                 (1.0f - text.pivot.x) * size.x, text.pivot.y * size.y);
	}

	/// The text's box corners in the world: top-left, top-right, bottom-right, bottom-left.
	inline std::array<glm::vec3, 4> WorldTextBoxCorners(const WorldText& text, const WorldUIPlane& plane) {
		return WorldUIBoxCorners(plane, WorldTextBoxInPlane(text));
	}

	inline bool RayHitsWorldTextBox(const WorldText& text, const WorldUIPlane& plane, const glm::vec3& origin, const glm::vec3& direction, float& t) {
		return RayHitsWorldUIBox(plane, WorldTextBoxInPlane(text), origin, direction, t);
	}

	// ---- World-space UICanvas -------------------------------------------------------------------

	inline WorldUIPlane WorldCanvasPlaneFor(const UICanvas& canvas, const glm::mat4& world, const glm::vec3& cameraPosition, const glm::mat4& view) {
		return WorldUIPlaneFor(canvas.billboard, world, cameraPosition, view);
	}

	/// A page point (canvas pixels, origin top-left, +y down) in the canvas's plane (metres, +y up)
	/// - what UI::BuildWorldCanvasPanel does to every vertex.
	inline glm::vec2 WorldCanvasToPlane(const UICanvas& canvas, glm::vec2 pagePoint) {
		const glm::vec2 page   = glm::max(canvas.referenceResolution, glm::vec2(0.0f));
		const float     ppu    = std::max(canvas.pixelsPerUnit, 1.0e-3f);
		const glm::vec2 origin = canvas.pivot * page;
		return glm::vec2((pagePoint.x - origin.x) / ppu, (origin.y - pagePoint.y) / ppu);
	}

	/// The other way: a point of the canvas's plane on its page (canvas pixels, +y down).
	inline glm::vec2 WorldCanvasToPage(const UICanvas& canvas, glm::vec2 planePoint) {
		const glm::vec2 page   = glm::max(canvas.referenceResolution, glm::vec2(0.0f));
		const float     ppu    = std::max(canvas.pixelsPerUnit, 1.0e-3f);
		const glm::vec2 origin = canvas.pivot * page;
		return glm::vec2(origin.x + planePoint.x * ppu, origin.y - planePoint.y * ppu);
	}

	/// The whole page in the canvas's plane: min x, min y, max x, max y.
	inline glm::vec4 WorldCanvasBoxInPlane(const UICanvas& canvas) {
		const glm::vec2 page = glm::max(canvas.referenceResolution, glm::vec2(0.0f));
		const glm::vec2 a = WorldCanvasToPlane(canvas, { 0.0f, page.y });   // bottom-left
		const glm::vec2 b = WorldCanvasToPlane(canvas, { page.x, 0.0f });   // top-right
		return glm::vec4(a, b);
	}

}   // NAMESPACE CRUCIBLE
