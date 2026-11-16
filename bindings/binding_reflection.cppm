export module binding_reflection;

import std;

export {
  consteval const char* getIdentifier(std::meta::info entity) {
    return std::define_static_string(std::meta::identifier_of(entity));
  }

  consteval auto getFields(std::meta::info type) {
    return std::define_static_array(
        std::meta::nonstatic_data_members_of(type, std::meta::access_context::unprivileged())
    );
  }

  consteval bool isTemplateSpecializationOf(std::meta::info type, std::meta::info typeTemplate) {
    type = std::meta::dealias(type);
    return std::meta::has_template_arguments(type) && std::meta::template_of(type) == typeTemplate;
  }
}
