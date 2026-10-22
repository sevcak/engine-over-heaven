#pragma once

#include "string_utils.hpp"
#include <core/types.hpp>

class CVarParameter;

class CVarSystem
{
public:
    static CVarSystem *get();

    virtual CVarParameter *get_cvar(StringUtils::StringHash hash) = 0;

    virtual f64 *get_float_cvar(StringUtils::StringHash hash) = 0;

    virtual i32 *get_int_cvar(StringUtils::StringHash hash) = 0;

    virtual const char *get_string_cvar(StringUtils::StringHash hash) = 0;

    virtual void set_float_cvar(StringUtils::StringHash hash, f64 value) = 0;

    virtual void set_int_cvar(StringUtils::StringHash hash, i32 value) = 0;

    virtual void set_string_cvar(StringUtils::StringHash hash, const char *value) = 0;

    virtual CVarParameter *create_float_cvar(
        const char *name, const char *description, f64 default_value, f64 current_value) = 0;

    virtual CVarParameter *create_int_cvar(const char *name, const char *description,
        i32 default_value, i32 current_value) = 0;

    virtual CVarParameter *create_string_cvar(const char *name, const char *description,
        const char *default_value, const char *current_value) = 0;

    virtual void draw_imgui_editor() = 0;
};

enum class CVarFlags : u32
{
    None = 0,
    Noedit = 1 << 1,
    EditReadOnly = 1 << 2,
    Advanced = 1 << 3,

    EditCheckbox = 1 << 8,
    EditFloatDrag = 1 << 9,
};

template <typename T>
struct AutoCVar
{
protected:
    int index;
    using CVarType = T;
};

struct AutoCVar_Float : AutoCVar<f64>
{
    AutoCVar_Float(const char *name, const char *description, f64 default_vlaue,
        CVarFlags flags = CVarFlags::None);

    f64 get();

    void set(f64 val);
};

struct AutoCVar_Int : AutoCVar<i32>
{
    AutoCVar_Int(const char *name, const char *description, i32 default_value,
        CVarFlags flags = CVarFlags::None);

    i32 get();

    void set(i32 val);
};

struct AutoCVar_String : AutoCVar<std::string>
{
    AutoCVar_String(const char *name, const char *description, const char *default_value,
        CVarFlags flags = CVarFlags::None);

    const char *get();

    void set(const std::string val);
};