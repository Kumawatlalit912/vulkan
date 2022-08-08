#pragma once

#include "shaderbuilder/ShaderVariableLayouts.h"

struct VertexData;

LAYOUT_DECLARATION(VertexData, per_vertex_data)
{
  static constexpr auto layouts = make_layouts(
    MEMBER(vec4, m_position),
    MEMBER(vec2, m_texture_coordinates)
  );
};

// Struct describing data type and format of vertex attributes.
struct VertexData
{
  glsl::vec4 m_position;
  glsl::vec2 m_texture_coordinates;
};
