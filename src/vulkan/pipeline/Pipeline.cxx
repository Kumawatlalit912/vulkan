#include "sys.h"
#include "Pipeline.h"
#include "SynchronousWindow.h"
#include "utils/malloc_size.h"
#include "debug.h"

namespace vulkan::pipeline {

std::string_view Pipeline::preprocess(
    shaderbuilder::ShaderInfo const& shader_info,
    std::string& glsl_source_code_buffer,
    std::set<shaderbuilder::VertexAttribute> const* vertex_attributes)
{
  DoutEntering(dc::vulkan, "Pipeline::preprocess(" << shader_info << ", glsl_source_code_buffer, " << vertex_attributes << ") [" << this << "]");

  std::string_view source = shader_info.glsl_template_code();

  // Assume no preprocessing is necessary if the source already starts with "#version".
  if (source.starts_with("#version"))
    return source;

  static constexpr char const* version_header = "#version 450\n\n";
  size_t source_code_size_estimate = std::strlen(version_header) + source.length();

  std::string declarations;
  bool const has_vertex_attributes = vertex_attributes && !vertex_attributes->empty();
  if (has_vertex_attributes)
  {
    for (shaderbuilder::VertexAttribute const& vertex_attribute : *vertex_attributes)
      declarations += vertex_attribute.declaration(this);
    declarations += '\n';
  }
  source_code_size_estimate += declarations.size();

  glsl_source_code_buffer.reserve(utils::malloc_size(source_code_size_estimate) - 1);
  glsl_source_code_buffer = version_header;
  glsl_source_code_buffer += declarations;

  // Store each position where a match string occurs, together with an std::pair
  // containing the found substring that has to be replaced (first) and the
  // string that it has to be replaced with (second).
  std::map<size_t, std::pair<std::string, std::string>> positions;

  if (has_vertex_attributes)
  {
    // vertex_attributes contains a number of strings that we need to find in the source.
    // They may occur zero or more times.
    for (auto&& vertex_attribute : *vertex_attributes)
    {
      std::string const match_string = vertex_attribute.layout().m_glsl_id_str;
      for (size_t pos = 0; (pos = source.find(match_string, pos)) != std::string_view::npos; pos += match_string.length())
        positions[pos] = std::make_pair(match_string, vertex_attribute.layout().name());
    }
  }

  // Next copy alternating, the characters in between the strings and the replacements of the substrings.
  size_t start = 0;
  for (auto&& p : positions)
  {
    // Copy the characters leading up to the string at position p.
    glsl_source_code_buffer += source.substr(start, p.first - start);
    // Advance start to just after the found string.
    start = p.first + p.second.first.length();
    // Append the replacement string.
    glsl_source_code_buffer += p.second.second;
  }
  // Copy the remaining characters.
  glsl_source_code_buffer += source.substr(start);
  glsl_source_code_buffer.shrink_to_fit();
  return glsl_source_code_buffer;
}

std::vector<vk::VertexInputBindingDescription> Pipeline::vertex_binding_descriptions() const
{
  DoutEntering(dc::vulkan, "Pipeline::vertex_binding_descriptions() [" << this << "]");
  std::vector<vk::VertexInputBindingDescription> vertex_binding_descriptions;
  uint32_t binding = 0;
  for (auto const* vextex_input_set : m_vertex_shader_input_sets)
  {
    vertex_binding_descriptions.push_back({
        .binding = binding,
        .stride = vextex_input_set->chunk_size(),
        .inputRate = vextex_input_set->input_rate()});
    ++binding;
  }
  return vertex_binding_descriptions;
}

std::vector<vk::VertexInputAttributeDescription> Pipeline::vertex_input_attribute_descriptions() const
{
  std::vector<vk::VertexInputAttributeDescription> vertex_input_attribute_descriptions;
  for (shaderbuilder::VertexAttribute const& vertex_attribute : m_vertex_attributes)
  {
    auto location = m_vertex_shader_location_context.locations.find(&vertex_attribute);
    ASSERT(location != m_vertex_shader_location_context.locations.end());
    shaderbuilder::TypeInfo type_info(vertex_attribute.layout().m_base_type);
    vertex_input_attribute_descriptions.push_back(vk::VertexInputAttributeDescription{
        .location = location->second,
        .binding = static_cast<uint32_t>(vertex_attribute.binding().get_value()),
        .format = type_info.format,
        .offset = vertex_attribute.layout().m_offset});
  }
  return vertex_input_attribute_descriptions;
}

void Pipeline::build_shader(task::SynchronousWindow const* owning_window,
    shaderbuilder::ShaderIndex const& shader_index, shaderbuilder::ShaderCompiler const& compiler, shaderbuilder::SPIRVCache& spirv_cache
    COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix))
{
  std::string glsl_source_code_buffer;
  std::string_view glsl_source_code;
  shaderbuilder::ShaderInfo const& shader_info = owning_window->application().get_shader_info(shader_index);
  switch (shader_info.stage())
  {
    case vk::ShaderStageFlagBits::eVertex:
      glsl_source_code = preprocess(shader_info, glsl_source_code_buffer, &m_vertex_attributes);
      break;
    default:
      glsl_source_code = preprocess(shader_info, glsl_source_code_buffer);
      break;
  }

  // Add a shader module to this pipeline.
  spirv_cache.compile(glsl_source_code, compiler, shader_info);
  m_unique_handles.push_back(spirv_cache.create_module({}, owning_window->logical_device()
      COMMA_CWDEBUG_ONLY(ambifix(".m_unique_handles[" + std::to_string(m_unique_handles.size()) + "]"))));
  m_shader_stage_create_infos.push_back(
    {
      .flags = vk::PipelineShaderStageCreateFlags(0),
      .stage = shader_info.stage(),
      .module = *m_unique_handles.back(),
      .pName = "main"
    }
  );
}

} // namespace vulkan::pipeline
