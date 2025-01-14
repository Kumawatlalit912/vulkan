#pragma once

#include "SetKeyToSetIndexHint.h"
#include "utils/UniqueID.h"
#include "utils/Badge.h"
#include "utils/Singleton.h"

namespace vulkan::descriptor {
class SetKey;

class SetKeyContext : public Singleton<SetKeyContext>
{
  friend_Instance;
 private:
  SetKeyContext() = default;
  ~SetKeyContext() = default;
  SetKeyContext(SetKeyContext const&) = delete;

 private:
  utils::UniqueIDContext<std::size_t> m_set_key_context;
  SetKeyToSetIndexHint m_set_key_to_set_index;      // Map from keys of this context to indexes.

 public:
  utils::UniqueID<std::size_t> get_id(utils::Badge<SetKey>) { return m_set_key_context.get_id(); }
};

} // namespace vulkan::descriptor
