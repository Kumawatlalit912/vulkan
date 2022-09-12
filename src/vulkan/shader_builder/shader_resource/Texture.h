#pragma once

#include "memory/Image.h"
#include "descriptor/SetKey.h"
#include "descriptor/SetKeyContext.h"
#include "ShaderResource.h"

namespace vulkan::shader_builder::shader_resource {

// Data collection used for textures.
struct Texture : public memory::Image
{
 private:
  descriptor::SetKey    m_descriptor_set_key;
  ShaderResource::members_container_t m_members;        // A fake map with just one element.
  std::unique_ptr<char const[]> m_glsl_id_full;
  vk::UniqueImageView   m_image_view;
  vk::UniqueSampler     m_sampler;

 public:
  // Used to move-assign later.
  Texture() = default;
  ~Texture() { DoutEntering(dc::vulkan, "shader_resource::Texture::~Texture() [" << this << "]"); }

  // Use sampler as-is.
  Texture(
      std::string_view glsl_id_full_postfix,
      LogicalDevice const* logical_device,
      vk::Extent2D extent,
      vulkan::ImageViewKind const& image_view_kind,
      vk::UniqueSampler&& sampler,
      MemoryCreateInfo memory_create_info
      COMMA_CWDEBUG_ONLY(Ambifix const& ambifix)) :
    m_descriptor_set_key(descriptor::SetKeyContext::instance()),
    memory::Image(logical_device, extent, image_view_kind, memory_create_info
        COMMA_CWDEBUG_ONLY(ambifix)),
    m_image_view(logical_device->create_image_view(m_vh_image, image_view_kind
        COMMA_CWDEBUG_ONLY(".m_image_view" + ambifix))),
    m_sampler(std::move(sampler))
  {
    DoutEntering(dc::vulkan, "shader_resource::Texture::Texture(\"" << glsl_id_full_postfix << "\", " << logical_device << ", " << extent <<
        ", " << image_view_kind << ", @" << &sampler << ", memory_create_info) [" << this << "]");
    std::string glsl_id_full("Texture::");
    glsl_id_full.append(glsl_id_full_postfix);
    auto glsl_id_full_ptr = std::make_unique<char[]>(glsl_id_full.size() + 1);
    strcpy(glsl_id_full_ptr.get(), glsl_id_full.data());
    m_glsl_id_full = std::move(glsl_id_full_ptr);

    // Add a fake ShaderResourceMember.
    ShaderResourceMember fake_member(m_glsl_id_full.get(), 0, {}, 0);
    m_members.insert(std::make_pair(0, fake_member));
  }

  // Create sampler too.
  Texture(
      char const* glsl_id_full_postfix,
      LogicalDevice const* logical_device,
      vk::Extent2D extent,
      vulkan::ImageViewKind const& image_view_kind,
      SamplerKind const& sampler_kind,
      GraphicsSettingsPOD const& graphics_settings,
      MemoryCreateInfo memory_create_info
      COMMA_CWDEBUG_ONLY(Ambifix const& ambifix)) :
    Texture(glsl_id_full_postfix, logical_device, extent, image_view_kind,
        logical_device->create_sampler(sampler_kind, graphics_settings COMMA_CWDEBUG_ONLY(".m_sampler" + ambifix)),
        memory_create_info
        COMMA_CWDEBUG_ONLY(ambifix))
  {
  }

  // Create sampler too, allowing to pass an initializer list to construct the SamplerKind (from temporary SamplerKindPOD).
  Texture(
      char const* glsl_id_full_postfix,
      LogicalDevice const* logical_device,
      vk::Extent2D extent,
      vulkan::ImageViewKind const& image_view_kind,
      SamplerKindPOD const&& sampler_kind,
      GraphicsSettingsPOD const& graphics_settings,
      MemoryCreateInfo memory_create_info
      COMMA_CWDEBUG_ONLY(Ambifix const& ambifix)) :
    Texture(glsl_id_full_postfix, logical_device, extent, image_view_kind,
        { logical_device, std::move(sampler_kind) }, graphics_settings,
        memory_create_info
        COMMA_CWDEBUG_ONLY(ambifix))
  {
  }

  // Class is move-only.
  Texture(Texture&& rhs) = default;
  Texture& operator=(Texture&& rhs) = default;

  // Accessors.
  char const* glsl_id_full() const { return m_glsl_id_full.get(); }
  ShaderResource::members_container_t* members() { return &m_members; }
  vk::ImageView image_view() const { return *m_image_view; }
  vk::Sampler sampler() const { return *m_sampler; }

  descriptor::SetKey descriptor_set_key() const { return m_descriptor_set_key; }
};

} // namespace vulkan::shader_builder::shader_resource
