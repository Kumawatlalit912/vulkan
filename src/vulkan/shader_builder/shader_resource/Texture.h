#pragma once

#include "Base.h"
#include "ShaderResourceMember.h"
#include "memory/Image.h"
#include "descriptor/SetKey.h"
#include "descriptor/SetKeyContext.h"

namespace vulkan::shader_builder::shader_resource {

namespace detail {

// A wrapper around ShaderResourceMember, which doesn't have
// a default constructor, together with the run-time constructed
// glsl id string, so that below we only need a single memory
// allocation.
class TextureShaderResourceMember
{
 private:
  ShaderResourceMember m_member;
  char m_glsl_id_full[];

  TextureShaderResourceMember(std::string const& glsl_id_full) : m_member(m_glsl_id_full)
  {
    strcpy(m_glsl_id_full, glsl_id_full.data());
  }

 public:
  static std::unique_ptr<TextureShaderResourceMember> create(std::string const& glsl_id_full)
  {
    void* memory = malloc(sizeof(ShaderResourceMember) + glsl_id_full.size() + 1);
    return std::unique_ptr<TextureShaderResourceMember>{new (memory) TextureShaderResourceMember(glsl_id_full)};
  }

  void operator delete(void* p)
  {
    free(p);
  }

  // Accessor.
  ShaderResourceMember const& member() const { return m_member; }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const;
#endif
};

} // namespace detail

// Data collection used for textures.
struct Texture : public Base, public memory::Image
{
 private:
  std::unique_ptr<detail::TextureShaderResourceMember> m_member;        // A Texture only has a single "member".
  vk::UniqueImageView   m_image_view;
  vk::UniqueSampler     m_sampler;

 public:
  // Used to move-assign later.
  Texture(char const* CWDEBUG_ONLY(debug_name)) : Base(descriptor::SetKeyContext::instance() COMMA_CWDEBUG_ONLY(debug_name)) { }
  ~Texture() { DoutEntering(dc::vulkan, "shader_resource::Texture::~Texture() [" << this << "]"); }

  // Use sampler as-is.
  Texture(
      std::string_view glsl_id_full_postfix,
      LogicalDevice const* logical_device,
      vk::Extent2D extent,
      vulkan::ImageViewKind const& image_view_kind,
      vk::UniqueSampler&& sampler,
      MemoryCreateInfo memory_create_info) :
    memory::Image(logical_device, extent, image_view_kind, memory_create_info
        COMMA_CWDEBUG_ONLY(std::string{"FIXME"})),
    m_image_view(logical_device->create_image_view(m_vh_image, image_view_kind
        COMMA_CWDEBUG_ONLY(std::string{".m_image_view FIXME"} /*+ ambifix*/))),
    m_sampler(std::move(sampler))
  {
    DoutEntering(dc::vulkan, "shader_resource::Texture::Texture(\"" << glsl_id_full_postfix << "\", " << logical_device << ", " << extent <<
        ", " << image_view_kind << ", @" << &sampler << ", memory_create_info) [" << this << "]");
    std::string glsl_id_full("Texture::");
    glsl_id_full.append(glsl_id_full_postfix);
    m_member = detail::TextureShaderResourceMember::create(glsl_id_full);
  }

  // Create sampler too.
  Texture(
      char const* glsl_id_full_postfix,
      LogicalDevice const* logical_device,
      vk::Extent2D extent,
      vulkan::ImageViewKind const& image_view_kind,
      SamplerKind const& sampler_kind,
      GraphicsSettingsPOD const& graphics_settings,
      MemoryCreateInfo memory_create_info) :
    Texture(glsl_id_full_postfix, logical_device, extent, image_view_kind,
        logical_device->create_sampler(sampler_kind, graphics_settings COMMA_CWDEBUG_ONLY(std::string{".m_sampler FIXME" /*+ ambifix*/})),
        memory_create_info)
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
      MemoryCreateInfo memory_create_info) :
    Texture(glsl_id_full_postfix, logical_device, extent, image_view_kind,
        { logical_device, std::move(sampler_kind) }, graphics_settings,
        memory_create_info)
  {
  }

  // Class is move-only.
  // Note: do NOT move the Ambifix and/or Base::m_descriptor_set_key!
  // Those are only initialized in-place with the constructor that takes just the Ambifix.
  Texture(Texture&& rhs) : Base(std::move(rhs)), Image(std::move(rhs)), m_member(std::move(rhs.m_member)), m_image_view(std::move(rhs.m_image_view)), m_sampler(std::move(rhs.m_sampler)) { }
  Texture& operator=(Texture&& rhs)
  {
    this->Base::operator=(std::move(rhs));
    this->memory::Image::operator=(std::move(rhs));
    m_member = std::move(rhs.m_member);
    m_image_view = std::move(rhs.m_image_view);
    m_sampler = std::move(rhs.m_sampler);
    return *this;
  }

  void create(task::SynchronousWindow const* owning_window) override;
  void update_descriptor_set(task::SynchronousWindow const* owning_window, vk::DescriptorSet vh_descriptor_set, uint32_t binding) const override;
  void ready() override { } //FIXME: isn't it better to *always* notified that the Texture is bound to a descriptor set?

  // Accessors.
  char const* glsl_id_full() const { return m_member->member().glsl_id_full(); }
  ShaderResourceMember const& member() const { return m_member->member(); }
  vk::ImageView image_view() const { return *m_image_view; }
  vk::Sampler sampler() const { return *m_sampler; }

#ifdef CWDEBUG
  void print_on(std::ostream& os) const override;
#endif
};

} // namespace vulkan::shader_builder::shader_resource
