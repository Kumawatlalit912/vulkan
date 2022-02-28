#include "sys.h"
#include "Pipeline.h"
#include "SynchronousWindow.h"
#include "utils/malloc_size.h"
#include "debug.h"

namespace vulkan::pipeline {

std::string_view Pipeline::preprocess(
    shaderbuilder::ShaderInfo const& shader_info,
    std::string& glsl_source_code_buffer,
    std::set<shaderbuilder::VertexAttributeEntry> const* vertex_attributes)
{
  DoutEntering(dc::vulkan, "Pipeline::preprocess(" << shader_info << ", glsl_source_code_buffer, " << vertex_attributes << ") [" << this << "]");

  std::string_view source = shader_info.glsl_template_code();

  // Assume no preprocessing is necessary if the source already starts with "#version".
  if (source.starts_with("#version"))
    return source;

  size_t source_code_size_estimate = 15 + source.length();

  if (vertex_attributes)
    source_code_size_estimate += 64 * vertex_attributes->size();

  glsl_source_code_buffer.reserve(utils::malloc_size(source_code_size_estimate) - 1);
  glsl_source_code_buffer = "#version 450\n\n";

  if (vertex_attributes)
  {
    // Add declarations.
    for (auto&& attribute : *vertex_attributes)
      glsl_source_code_buffer += attribute.vertex_attribute.declaration(m_vertex_shader_location_context);

    if (!vertex_attributes->empty())
      glsl_source_code_buffer += '\n';
  }

  // Store each position where a match string occurs, together with an std::pair
  // containing the found substring that has to be replaced (first) and the
  // string that it has to be replaced with (second).
  std::map<size_t, std::pair<std::string, std::string>> positions;

  if (vertex_attributes)
  {
    // vertex_attributes contains a number of strings that we need to find in source.
    // They may occur zero or more times.
    for (auto&& attribute : *vertex_attributes)
    {
      std::string const match_string = attribute.vertex_attribute.m_glsl_id_str;
      for (size_t pos = 0; (pos = source.find(match_string, pos)) != std::string_view::npos; pos += match_string.length())
        positions[pos] = std::make_pair(match_string, attribute.vertex_attribute.name());
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
        .stride = vextex_input_set->size(),
        .inputRate = vextex_input_set->input_rate()});
    ++binding;
  }
  return vertex_binding_descriptions;
}

std::vector<vk::VertexInputAttributeDescription> Pipeline::vertex_attribute_descriptions() const
{
  std::vector<vk::VertexInputAttributeDescription> vertex_attribute_descriptions;
  for (auto&& vertex_attribute_entry : m_vertex_attributes)
  {
    shaderbuilder::VertexAttribute const* vertex_attribute = &vertex_attribute_entry.vertex_attribute;
    auto location = m_vertex_shader_location_context.locations.find(vertex_attribute);
    ASSERT(location != m_vertex_shader_location_context.locations.end());
    shaderbuilder::TypeInfo type_info(vertex_attribute_entry.vertex_attribute.m_glsl_type);
    vertex_attribute_descriptions.push_back(vk::VertexInputAttributeDescription{
        .location = location->second,
        .binding = static_cast<uint32_t>(vertex_attribute_entry.binding.get_value()),
        .format = type_info.format,
        .offset = vertex_attribute_entry.vertex_attribute.m_offset});
    Dout(dc::always, "ShaderModule::vertex_attribute_descriptions: added " << vertex_attribute_descriptions.back());
  }
  return vertex_attribute_descriptions;
}

void Pipeline::build_shader(shaderbuilder::ShaderInfo const& shader_info, shaderbuilder::ShaderCompiler const& compiler
    COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix))
{
  shaderbuilder::ShaderModule shader_module;

  std::string glsl_source_code_buffer;
  std::string_view glsl_source_code;
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
  shader_module.compile(glsl_source_code, compiler, shader_info);
  m_unique_handles.push_back(shader_module.create({}, m_owning_window
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

void Pipeline::generate(task::SynchronousWindow* owning_window COMMA_CWDEBUG_ONLY(AmbifixOwner const& ambifix))
{
  DoutEntering(dc::vulkan, "Pipeline::generate(" << owning_window << ") [" << this << "]");

  for (shaderbuilder::VertexShaderInputSetBase* vertex_shader_input_set : m_vertex_shader_input_sets)
  {
    size_t entry_size = vertex_shader_input_set->size();
    int count = vertex_shader_input_set->count();
    size_t buffer_size = count * entry_size;

    // FIXME: write directly into allocated vulkan buffer?
    std::vector<std::byte> buf(buffer_size);
    std::byte const* const end = buf.data() + buffer_size;
    int batch_size;
    for (std::byte* ptr = buf.data(); ptr != end; ptr += batch_size * entry_size)
    {
      batch_size = vertex_shader_input_set->next_batch();
      vertex_shader_input_set->get_input_entry(ptr);
    }

    // FIXME: do we really have to keep this object around?
    m_buffers.emplace_back(owning_window->logical_device().create_buffer(buffer_size,
        vk::BufferUsageFlagBits::eTransferDst | vk::BufferUsageFlagBits::eVertexBuffer, vk::MemoryPropertyFlagBits::eDeviceLocal
        COMMA_CWDEBUG_ONLY(ambifix("m_vertex_buffer"))));

    m_vhv_buffers.push_back(*m_buffers.back().m_buffer);

    owning_window->copy_data_to_buffer(buffer_size, buf.data(), *m_buffers.back().m_buffer, 0, vk::AccessFlags(0),
        vk::PipelineStageFlagBits::eTopOfPipe, vk::AccessFlagBits::eVertexAttributeRead, vk::PipelineStageFlagBits::eVertexInput);
  }
}

} // namespace vulkan::pipeline
