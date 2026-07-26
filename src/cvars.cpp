#include "cvars.hpp"
#include "imgui.h"

#include <cassert>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

inline bool has_flag(CVarFlags flags, CVarFlags flag)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(flag)) != 0;
}

enum class CVarType : char
{
    INT,
    FLOAT,
    STRING
};

class CVarParameter
{
public:
    friend class CVarSystemImpl;

    int32_t array_index;

    CVarType type;
    CVarFlags flags;
    std::string name;
    std::string description;
};

template <typename T>
struct CVarStorage
{
    T initial;
    T current;
    CVarParameter *parameter;
};

template <typename T>
struct CVarArray
{
    std::vector<CVarStorage<T>> cvars;

    CVarArray(size_t capacity) { cvars.reserve(capacity); }

    CVarStorage<T> *get_current_storage(int32_t index) { return &cvars[index]; }
    const CVarStorage<T> *get_current_storage(int32_t index) const { return &cvars[index]; }

    T *get_current_ptr(int32_t index) { return &cvars[index].current; }
    const T *get_current_ptr(int32_t index) const { return &cvars[index].current; }

    T get_current(int32_t index) const { return cvars[index].current; }

    void set_current(int32_t index, const T &value) { cvars[index].current = value; }

    int32_t add(const T &value, CVarParameter *param) { return add(value, value, param); }

    int32_t add(const T &initial_value, const T &current_value, CVarParameter *param)
    {
        assert(cvars.size() < cvars.capacity() && "CVarArray out of capacity.");

        int32_t index = static_cast<int32_t>(cvars.size());

        cvars.push_back({ .initial = initial_value, .current = current_value, .parameter = param });

        param->array_index = index;

        return index;
    }
};

class CVarSystemImpl : public CVarSystem
{
public:
    static CVarSystemImpl *get() { return static_cast<CVarSystemImpl *>(CVarSystem::get()); }

    constexpr static int MAX_INT_CVARS = 1000;
    CVarArray<int32_t> int_cvars { MAX_INT_CVARS };

    constexpr static int MAX_FLOAT_CVARS = 1000;
    CVarArray<double> float_cvars { MAX_FLOAT_CVARS };

    constexpr static int MAX_STRING_CVARS = 200;
    CVarArray<std::string> string_cvars { MAX_STRING_CVARS };

    template <typename T>
    CVarArray<T> *get_cvar_array();

    template <>
    CVarArray<int32_t> *get_cvar_array()
    {
        return &int_cvars;
    }

    template <>
    CVarArray<double> *get_cvar_array()
    {
        return &float_cvars;
    }

    template <>
    CVarArray<std::string> *get_cvar_array()
    {
        return &string_cvars;
    }

    CVarParameter *create_float_cvar(const char *name, const char *description,
        double default_value, double current_value) override final
    {
        CVarParameter *param = init_cvar(name, description);
        if (!param) {
            return nullptr;
        }

        param->type = CVarType::FLOAT;

        get_cvar_array<double>()->add(default_value, current_value, param);

        return param;
    }

    CVarParameter *create_int_cvar(const char *name, const char *description, int32_t default_value,
        int32_t current_value) override final
    {
        CVarParameter *param = init_cvar(name, description);
        if (!param) {
            return nullptr;
        }

        param->type = CVarType::INT;

        get_cvar_array<int32_t>()->add(default_value, current_value, param);

        return param;
    }

    CVarParameter *create_string_cvar(const char *name, const char *description,
        const char *default_value, const char *current_value) override final
    {
        CVarParameter *param = init_cvar(name, description);
        if (!param) {
            return nullptr;
        }

        param->type = CVarType::STRING;

        get_cvar_array<std::string>()->add(default_value, current_value, param);

        return param;
    }

    CVarParameter *get_cvar(StringUtils::StringHash hash) override final
    {
        auto it = saved_cvars.find(hash);

        if (it != saved_cvars.end()) {
            return &(*it).second;
        }

        return nullptr;
    }

    double *get_float_cvar(StringUtils::StringHash hash) override final
    {
        return get_cvar_current<double>(hash);
    }

    int32_t *get_int_cvar(StringUtils::StringHash hash) override final
    {
        return get_cvar_current<int32_t>(hash);
    }

    const char *get_string_cvar(StringUtils::StringHash hash) override final
    {
        return get_cvar_current<std::string>(hash)->c_str();
    }

    template <typename T>
    T *get_cvar_current(uint32_t namehash)
    {
        CVarParameter *param = get_cvar(namehash);
        if (!param) {
            return nullptr;
        }

        return get_cvar_array<T>()->get_current_ptr(param->array_index);
    }

    void set_float_cvar(StringUtils::StringHash hash, double value) override final
    {
        set_cvar_current<double>(hash, value);
    }

    void set_int_cvar(StringUtils::StringHash hash, int32_t value) override final
    {
        set_cvar_current<int32_t>(hash, value);
    }

    void set_string_cvar(StringUtils::StringHash hash, const char *value) override final
    {
        set_cvar_current<std::string>(hash, value);
    }

    template <typename T>
    void set_cvar_current(StringUtils::StringHash hash, const T &value)
    {
        CVarParameter *cvar = get_cvar(hash);
        if (cvar) {
            get_cvar_array<T>()->set_current(cvar->array_index, value);
        }
    }

    void draw_imgui_editor() override final
    {
        ImGui::Begin("CVar Editor");

        static ImGuiTextFilter filter;
        filter.Draw("Filter");
        ImGui::Separator();

        for (auto &[hash, param] : saved_cvars) {
            if (has_flag(param.flags, CVarFlags::Noedit)) {
                continue;
            }

            if (!filter.PassFilter(param.name.c_str())) {
                continue;
            }

            ImGui::PushID(param.name.c_str());

            bool is_readonly = has_flag(param.flags, CVarFlags::EditReadOnly);
            if (is_readonly) {
                ImGui::BeginDisabled();
            }

            switch (param.type) {
            case CVarType::INT: {
                int32_t *val = get_cvar_array<int32_t>()->get_current_ptr(param.array_index);

                if (has_flag(param.flags, CVarFlags::EditCheckbox)) {
                    bool b = (*val != 0);
                    if (ImGui::Checkbox(param.name.c_str(), &b)) {
                        *val = b ? 1 : 0;
                    }
                } else {
                    ImGui::InputInt(param.name.c_str(), val);
                }
                break;
            }
            case CVarType::FLOAT: {
                double *val = get_cvar_array<double>()->get_current_ptr(param.array_index);

                if (has_flag(param.flags, CVarFlags::EditFloatDrag)) {
                    ImGui::DragScalar(param.name.c_str(), ImGuiDataType_Double, val, 0.01f);
                } else {
                    ImGui::InputScalar(param.name.c_str(), ImGuiDataType_Double, val);
                }
                break;
            }
            case CVarType::STRING: {
                std::string *val =
                    get_cvar_array<std::string>()->get_current_ptr(param.array_index);

                // Local buffer for ImGui text editing.
                char buffer[256];
                strncpy(buffer, val->c_str(), sizeof(buffer));
                buffer[sizeof(buffer) - 1] = '\0';

                if (ImGui::InputText(param.name.c_str(), buffer, sizeof(buffer))) {
                    *val = buffer;
                }
                break;
            }
            }

            if (is_readonly) {
                ImGui::EndDisabled();
            }

            if (!param.description.empty() && ImGui::IsItemHovered()) {
                ImGui::SetTooltip("%s", param.description.c_str());
            }

            ImGui::PopID();
        }

        ImGui::End();
    }

private:
    CVarParameter *init_cvar(const char *name, const char *description)
    {
        if (get_cvar(name)) {
            return nullptr;
        }

        uint32_t namehash = StringUtils::StringHash { name };

        CVarParameter &new_param = saved_cvars[namehash];

        new_param.name = name;
        new_param.description = description;

        return &new_param;
    }

    std::unordered_map<uint32_t, CVarParameter> saved_cvars;
};

CVarSystem *CVarSystem::get()
{
    static CVarSystemImpl cvar_sys {};
    return &cvar_sys;
}

template <typename T>
T get_cvar_current_by_index(int32_t index)
{
    return CVarSystemImpl::get()->get_cvar_array<T>()->get_current(index);
}

template <typename T>
const T *get_cvar_current_ptr_by_index(int32_t index)
{
    return CVarSystemImpl::get()->get_cvar_array<T>()->get_current_ptr(index);
}

template <typename T>
void set_cvar_current_by_index(int32_t index, const T &data)
{
    CVarSystemImpl::get()->get_cvar_array<T>()->set_current(index, data);
}

AutoCVar_Float::AutoCVar_Float(
    const char *name, const char *description, double default_value, CVarFlags flags)
{
    CVarParameter *cvar =
        CVarSystem::get()->create_float_cvar(name, description, default_value, default_value);
    cvar->flags = flags;
    index = cvar->array_index;
}

double AutoCVar_Float::get()
{
    return get_cvar_current_by_index<CVarType>(index);
}

void AutoCVar_Float::set(double f)
{
    set_cvar_current_by_index<CVarType>(index, f);
}

AutoCVar_Int::AutoCVar_Int(
    const char *name, const char *description, int32_t default_value, CVarFlags flags)
{
    CVarParameter *cvar =
        CVarSystem::get()->create_int_cvar(name, description, default_value, default_value);
    cvar->flags = flags;
    index = cvar->array_index;
}

int32_t AutoCVar_Int::get()
{
    return get_cvar_current_by_index<CVarType>(index);
}

void AutoCVar_Int::set(int32_t i)
{
    set_cvar_current_by_index<CVarType>(index, i);
}

AutoCVar_String::AutoCVar_String(
    const char *name, const char *description, const char *default_value, CVarFlags flags)
{
    CVarParameter *cvar =
        CVarSystem::get()->create_string_cvar(name, description, default_value, default_value);
    cvar->flags = flags;
    index = cvar->array_index;
}

const char *AutoCVar_String::get()
{
    return get_cvar_current_ptr_by_index<CVarType>(index)->c_str();
}

void AutoCVar_String::set(const std::string s)
{
    set_cvar_current_by_index<CVarType>(index, s);
}